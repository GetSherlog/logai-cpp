#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <nlohmann/json.hpp>
#include "drain_parser.h"
#include "template_store.h"
#include "file_data_loader.h"
#include "log_parser.h"
#include "template_manager.h"
#include "gemini_vectorizer.h"
#include <curl/curl.h>
#include <sstream>
#include <vector>
#include <string>
#include <duckdb.hpp>

namespace py = pybind11;
using json = nlohmann::json;

// Global objects to maintain state between calls
static std::unique_ptr<logai::DrainParser> g_parser;
static std::unique_ptr<logai::TemplateStore> g_template_store;
static std::unique_ptr<logai::TemplateManager> g_template_manager;
static std::unique_ptr<logai::GeminiVectorizer> g_vectorizer;
static std::unique_ptr<duckdb::DuckDB> g_duckdb;
static std::vector<logai::LogParser::LogEntry> g_log_entries;

// Network configuration
static std::string g_milvus_host = "milvus";
static int g_milvus_port = 19530;

// Helper function for HTTP requests
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Function to generate embedding for a template using Gemini API
std::vector<float> generate_template_embedding(const std::string& template_text) {
    try {
        // Initialize Gemini vectorizer if not already done
        if (!g_vectorizer) {
            g_vectorizer = std::make_unique<logai::GeminiVectorizer>();
            if (!g_vectorizer->init()) {
                py::print("Failed to initialize Gemini vectorizer");
                return std::vector<float>();
            }
        }

        // Generate embedding
        std::vector<float> embedding;
        if (!g_vectorizer->generate_embedding(template_text, embedding)) {
            py::print("Failed to generate embedding using Gemini");
            return std::vector<float>();
        }

        return embedding;
    } catch (const std::exception& e) {
        py::print("Error generating embedding:", e.what());
        return std::vector<float>();
    }
}

// Initialize DuckDB store
bool init_duckdb() {
    try {
        if (!g_duckdb) {
            g_duckdb = std::make_unique<duckdb::DuckDB>("logai.db");
            
            // Create schema
            auto& conn = *g_duckdb;
            conn.Query(R"(
                CREATE TABLE IF NOT EXISTS log_entries (
                    id BIGINT PRIMARY KEY,
                    timestamp VARCHAR,
                    level VARCHAR,
                    message VARCHAR,
                    template_id VARCHAR
                )
            )");
        }
        
        py::print("Successfully initialized DuckDB store");
        return true;
    } catch (const std::exception& e) {
        py::print("Error initializing DuckDB:", e.what());
        return false;
    }
}

// Initialize Milvus connection
bool init_milvus(const std::string& host = "milvus", int port = 19530) {
    try {
        g_milvus_host = host;
        g_milvus_port = port;
        
        // Test connection
        CURL* curl = curl_easy_init();
        if (!curl) {
            py::print("Failed to initialize CURL");
            return false;
        }

        std::string url = "http://" + host + ":" + std::to_string(port) + "/health";
        std::string response;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            py::print("Failed to connect to Milvus server");
            return false;
        }
        
        py::print("Successfully connected to Milvus server");
        return true;
    } catch (const std::exception& e) {
        py::print("Error initializing Milvus connection:", e.what());
        return false;
    }
}

// Function to parse a log file and extract messages
bool parse_log_file(const std::string& file_path, const std::string& format) {
    // Create the appropriate data loader config
    logai::FileDataLoaderConfig config;
    config.encoding = "utf-8";
    
    if (!format.empty()) {
        config.format = format;
    }
    
    try {
        // Clear existing data
        g_log_entries.clear();
        
        // Initialize the file data loader
        logai::FileDataLoader loader(file_path, config);
        
        // Load the data
        loader.loadData(g_log_entries);
        
        py::print("Loaded", g_log_entries.size(), "log entries");
        
        // Initialize the DrainParser if it doesn't exist
        if (!g_parser) {
            logai::DataLoaderConfig dl_config;
            dl_config.format = config.format;
            g_parser = std::make_unique<logai::DrainParser>(dl_config);
        }
        
        // Process each log entry and store in both DuckDB and Milvus
        for (const auto& entry : g_log_entries) {
            // Store in DuckDB
            if (g_duckdb) {
                auto& conn = *g_duckdb;
                conn.Query(R"(
                    INSERT INTO log_entries (id, timestamp, level, message, template_id)
                    VALUES (?, ?, ?, ?, ?)
                )", entry.id, entry.timestamp, entry.level, entry.message, std::to_string(entry.template_id));
            }
            
            // Generate embedding for template and store in Milvus
            if (!entry.template.empty()) {
                std::vector<float> embedding = generate_template_embedding(entry.template);
                if (!embedding.empty()) {
                    CURL* curl = curl_easy_init();
                    if (curl) {
                        std::string url = "http://" + g_milvus_host + ":" + std::to_string(g_milvus_port) + "/insert";
                        json data = {
                            {"collection_name", "log_templates"},
                            {"vectors", {embedding}},
                            {"ids", {std::to_string(entry.template_id)}},
                            {"metas", {{"template", entry.template}}}
                        };
                        
                        std::string postData = data.dump();
                        std::string response;
                        
                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                        
                        curl_easy_perform(curl);
                        curl_easy_cleanup(curl);
                    }
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        py::print("Error loading log file:", e.what());
        return false;
    }
}

// Get Milvus connection string
std::string get_milvus_connection_string() {
    return g_milvus_host + ":" + std::to_string(g_milvus_port);
}

// Get DuckDB connection string
std::string get_duckdb_connection_string() {
    return "logai.db";
}

PYBIND11_MODULE(logai_cpp, m) {
    m.doc() = "Python bindings for LogAI C++ library";
    
    // Expose only the essential functions
    m.def("init_duckdb", &init_duckdb, "Initialize DuckDB store");
    m.def("init_milvus", &init_milvus, "Initialize Milvus connection",
          py::arg("host") = "milvus", py::arg("port") = 19530);
    m.def("parse_log_file", &parse_log_file, "Parse a log file",
          py::arg("file_path"), py::arg("format") = "");
    m.def("get_milvus_connection_string", &get_milvus_connection_string, 
          "Get Milvus connection string");
    m.def("get_duckdb_connection_string", &get_duckdb_connection_string,
          "Get DuckDB connection string");
} 