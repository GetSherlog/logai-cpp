#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <nlohmann/json.hpp>
#include "drain_parser.h"
#include "file_data_loader.h"
#include "log_parser.h"
#include "gemini_vectorizer.h"
#include <curl/curl.h>
#include <sstream>
#include <vector>
#include <string>
#include <duckdb.hpp>

namespace py = pybind11;
using json = nlohmann::json;

// Global objects to maintain state between calls
static std::unique_ptr<duckdb::DuckDB> g_duckdb;
static std::unique_ptr<duckdb::Connection> g_duckdb_conn;
static std::unique_ptr<logai::GeminiVectorizer> g_vectorizer;
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
            logai::GeminiVectorizerConfig config;
            g_vectorizer = std::make_unique<logai::GeminiVectorizer>(config);
        }

        // Generate embedding
        auto embedding_opt = g_vectorizer->get_embedding(template_text);
        if (!embedding_opt) {
            py::print("Failed to generate embedding using Gemini");
            return std::vector<float>();
        }

        return *embedding_opt;
    } catch (const std::exception& e) {
        py::print("Error generating embedding:", e.what());
        return std::vector<float>();
    }
}

// Initialize DuckDB store
bool init_duckdb(const std::string& db_path = "logai.db") {
    try {
        if (!g_duckdb) {
            g_duckdb = std::make_unique<duckdb::DuckDB>(db_path);
            g_duckdb_conn = std::make_unique<duckdb::Connection>(*g_duckdb);
            
            // Create schema for logs
            g_duckdb_conn->Query(R"(
                CREATE TABLE IF NOT EXISTS log_entries (
                    id INTEGER PRIMARY KEY,
                    timestamp VARCHAR,
                    level VARCHAR,
                    message VARCHAR,
                    template_id VARCHAR
                )
            )");
            
            // Create schema for templates
            g_duckdb_conn->Query(R"(
                CREATE TABLE IF NOT EXISTS log_templates (
                    template_id VARCHAR PRIMARY KEY,
                    template TEXT,
                    count INTEGER
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
bool parse_log_file(const std::string& file_path, const std::string& format, bool store_templates_in_milvus = true) {
    try {
        if (!g_duckdb_conn) {
            if (!init_duckdb()) {
                return false;
            }
        }
        
        // Create file data loader with appropriate configuration
        logai::FileDataLoaderConfig config;
        config.format = format.empty() ? "logfmt" : format;
        config.encoding = "utf-8";
        
        logai::FileDataLoader loader(file_path, config);
        
        // Use the optimized method to load logs directly to DuckDB
        std::string table_name = "log_entries";
        
        // Process file and store in DuckDB in chunks for memory efficiency
        bool result = loader.process_large_file(
            file_path,
            config.format,
            *g_duckdb_conn,
            table_name,
            2000,  // 2GB memory limit
            10000  // Process in chunks of 10,000 lines
        );
        
        // If requested and successful, also store templates in Milvus
        if (result && store_templates_in_milvus) {
            py::print("Extracting templates for vector storage...");
            
            // Query for unique templates from the processed data
            auto template_result = g_duckdb_conn->Query(R"(
                SELECT DISTINCT template_id, template 
                FROM log_templates
                WHERE template IS NOT NULL AND template != ''
            )");
            
            if (template_result->HasError()) {
                py::print("Error querying templates:", template_result->GetError());
            } else {
                // Process each template and store in Milvus
                size_t stored_count = 0;
                size_t failed_count = 0;
                
                for (size_t i = 0; i < template_result->RowCount(); i++) {
                    std::string template_id = template_result->GetValue(0, i).ToString();
                    std::string template_text = template_result->GetValue(1, i).ToString();
                    
                    // Generate embedding
                    std::vector<float> embedding = generate_template_embedding(template_text);
                    if (embedding.empty()) {
                        failed_count++;
                        continue;
                    }
                    
                    // Store in Milvus
                    CURL* curl = curl_easy_init();
                    if (!curl) {
                        py::print("Failed to initialize CURL");
                        failed_count++;
                        continue;
                    }
                    
                    std::string url = "http://" + g_milvus_host + ":" + std::to_string(g_milvus_port) + "/insert";
                    json data = {
                        {"collection_name", "log_templates"},
                        {"vectors", {embedding}},
                        {"ids", {template_id}},
                        {"metas", {{"template", template_text}}}
                    };
                    
                    std::string postData = data.dump();
                    std::string response;
                    
                    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                    
                    CURLcode res = curl_easy_perform(curl);
                    curl_easy_cleanup(curl);
                    
                    if (res == CURLE_OK) {
                        stored_count++;
                    } else {
                        failed_count++;
                    }
                }
                
                py::print("Stored", stored_count, "templates in Milvus, failed:", failed_count);
            }
        }
        
        return result;
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

// Function to run a SQL query on the DuckDB database
py::object run_duckdb_query(const std::string& query) {
    try {
        if (!g_duckdb_conn) {
            if (!init_duckdb()) {
                return py::none();
            }
        }
        
        auto result = g_duckdb_conn->Query(query);
        if (result->HasError()) {
            py::print("Query error:", result->GetError());
            return py::none();
        }
        
        // Convert result to a Python dictionary
        py::dict py_result;
        py::list rows;
        
        // Get column names
        auto& names = result->names;
        py::list column_names;
        for (const auto& name : names) {
            column_names.append(py::str(name));
        }
        py_result["columns"] = column_names;
        
        // Get rows
        for (size_t row_idx = 0; row_idx < result->RowCount(); row_idx++) {
            py::list row_data;
            for (size_t col_idx = 0; col_idx < result->ColumnCount(); col_idx++) {
                auto val = result->GetValue(col_idx, row_idx);
                if (val.IsNull()) {
                    row_data.append(py::none());
                } else {
                    switch (val.type().id()) {
                        case duckdb::LogicalTypeId::VARCHAR:
                            row_data.append(py::str(val.GetValue<std::string>()));
                            break;
                        case duckdb::LogicalTypeId::INTEGER:
                            row_data.append(py::int_(val.GetValue<int32_t>()));
                            break;
                        case duckdb::LogicalTypeId::BIGINT:
                            row_data.append(py::int_(val.GetValue<int64_t>()));
                            break;
                        case duckdb::LogicalTypeId::FLOAT:
                            row_data.append(py::float_(val.GetValue<float>()));
                            break;
                        case duckdb::LogicalTypeId::DOUBLE:
                            row_data.append(py::float_(val.GetValue<double>()));
                            break;
                        case duckdb::LogicalTypeId::BOOLEAN:
                            row_data.append(py::bool_(val.GetValue<bool>()));
                            break;
                        default:
                            row_data.append(py::str(val.ToString()));
                            break;
                    }
                }
            }
            rows.append(row_data);
        }
        py_result["data"] = rows;
        
        return py_result;
    } catch (const std::exception& e) {
        py::print("Error executing query:", e.what());
        return py::none();
    }
}

PYBIND11_MODULE(logai_cpp, m) {
    m.doc() = "Python bindings for LogAI C++ library";
    
    // Expose the essential functions
    m.def("init_duckdb", &init_duckdb, "Initialize DuckDB store",
          py::arg("db_path") = "logai.db");
    m.def("init_milvus", &init_milvus, "Initialize Milvus connection",
          py::arg("host") = "milvus", py::arg("port") = 19530);
    m.def("parse_log_file", &parse_log_file, "Parse a log file and store in DuckDB and Milvus",
          py::arg("file_path"), py::arg("format") = "", py::arg("store_templates_in_milvus") = true);
    m.def("get_milvus_connection_string", &get_milvus_connection_string, 
          "Get Milvus connection string");
    m.def("get_duckdb_connection_string", &get_duckdb_connection_string,
          "Get DuckDB connection string");
    m.def("run_duckdb_query", &run_duckdb_query, "Run SQL query on DuckDB database",
          py::arg("query"));
} 