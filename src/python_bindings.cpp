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
#include <folly/container/F14Map.h>

namespace py = pybind11;
using json = nlohmann::json;

// Global objects to maintain state between calls
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

// Function to parse a log file and return parsed records
py::list parse_log_file(const std::string& file_path, const std::string& format = "") {
    try {
        // Create file data loader with appropriate configuration
        logai::FileDataLoaderConfig config;
        config.format = format.empty() ? "logfmt" : format;
        config.encoding = "utf-8";
        
        logai::FileDataLoader loader(file_path, config);
        
        // Parse the log file and get records
        auto records = loader.parse_log_file(file_path, config.format);
        
        // Convert to Python list of dictionaries
        py::list result;
        for (const auto& record : records) {
            py::dict record_dict;
            record_dict["body"] = record.body;
            record_dict["template"] = record.template_str;
            record_dict["level"] = record.level;
            record_dict["message"] = record.message;
            
            // Convert timestamp if present
            if (record.timestamp) {
                auto time_t = std::chrono::system_clock::to_time_t(*record.timestamp);
                std::stringstream ss;
                ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
                record_dict["timestamp"] = ss.str();
            } else {
                record_dict["timestamp"] = py::none();
            }
            
            // Convert severity if present
            record_dict["severity"] = record.severity ? py::cast(*record.severity) : py::none();
            
            // Convert fields
            py::dict fields_dict;
            for (const auto& [key, value] : record.fields) {
                fields_dict[py::str(key.c_str())] = py::str(value.c_str());
            }
            record_dict["fields"] = fields_dict;
            
            result.append(record_dict);
        }
        
        return result;
    } catch (const std::exception& e) {
        py::print("Error parsing log file:", e.what());
        return py::list();
    }
}

// Process large log file with callback to Python
bool process_large_file_with_callback(const std::string& file_path, const std::string& format, py::function callback, int chunk_size = 10000) {
    try {
        // Create file data loader with appropriate configuration
        logai::FileDataLoaderConfig config;
        config.format = format.empty() ? "logfmt" : format;
        config.encoding = "utf-8";
        
        logai::FileDataLoader loader(file_path, config);
        
        // Create a C++ callback that calls the Python function
        auto cpp_callback = [&callback](const std::vector<logai::LogRecordObject>& batch) {
            // Convert batch to a Python list
            py::list py_batch;
            
            for (const auto& record : batch) {
                py::dict record_dict;
                record_dict["body"] = record.body;
                record_dict["template"] = record.template_str;
                record_dict["level"] = record.level;
                record_dict["message"] = record.message;
                
                // Convert timestamp if present
                if (record.timestamp) {
                    auto time_t = std::chrono::system_clock::to_time_t(*record.timestamp);
                    std::stringstream ss;
                    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
                    record_dict["timestamp"] = ss.str();
                } else {
                    record_dict["timestamp"] = py::none();
                }
                
                // Convert severity if present
                record_dict["severity"] = record.severity ? py::cast(*record.severity) : py::none();
                
                // Convert fields
                py::dict fields_dict;
                for (const auto& [key, value] : record.fields) {
                    fields_dict[py::str(key.c_str())] = py::str(value.c_str());
                }
                record_dict["fields"] = fields_dict;
                
                py_batch.append(record_dict);
            }
            
            // Call the Python function
            callback(py_batch);
        };
        
        // Process the file
        return loader.process_large_file_with_callback(
            file_path,
            config.format,
            chunk_size,
            cpp_callback
        );
    } catch (const std::exception& e) {
        py::print("Error processing log file:", e.what());
        return false;
    }
}

// Extract attributes from log lines
py::dict extract_attributes(const std::vector<std::string>& log_lines, const std::map<std::string, std::string>& patterns) {
    try {
        // Create a file data loader
        logai::FileDataLoaderConfig config;
        logai::FileDataLoader loader("", config);
        
        // Convert to F14FastMap for C++ function
        folly::F14FastMap<std::string, std::string> patterns_map(patterns.begin(), patterns.end());
        
        // Extract attributes
        auto result = loader.extract_attributes(log_lines, patterns_map);
        
        // Convert to Python dictionary
        py::dict py_result;
        for (const auto& [key, values] : result) {
            py::list py_values;
            for (const auto& value : values) {
                py_values.append(py::str(value));
            }
            py_result[py::str(key)] = py_values;
        }
        
        return py_result;
    } catch (const std::exception& e) {
        py::print("Error extracting attributes:", e.what());
        return py::dict();
    }
}

// Get Milvus connection string
std::string get_milvus_connection_string() {
    return g_milvus_host + ":" + std::to_string(g_milvus_port);
}

PYBIND11_MODULE(logai_cpp, m) {
    m.doc() = "LogAI C++ Module for Log Parsing and Analysis";
    
    // Parser functions
    m.def("parse_log_file", &parse_log_file, "Parse a log file and return parsed records",
          py::arg("file_path"), py::arg("format") = "");
    
    m.def("process_large_file_with_callback", &process_large_file_with_callback,
          "Process a large log file with a callback function for each batch of records",
          py::arg("file_path"), py::arg("format"), py::arg("callback"), py::arg("chunk_size") = 10000);
    
    // Attribute extraction
    m.def("extract_attributes", &extract_attributes, "Extract attributes from log lines using regex patterns",
          py::arg("log_lines"), py::arg("patterns"));
    
    // Milvus functions
    m.def("init_milvus", &init_milvus, "Initialize Milvus connection",
          py::arg("host") = "milvus", py::arg("port") = 19530);
    
    m.def("get_milvus_connection_string", &get_milvus_connection_string,
          "Get the Milvus connection string");
    
    // Embedding functions
    m.def("generate_template_embedding", &generate_template_embedding,
          "Generate embedding for a template using Gemini API",
          py::arg("template_text"));
} 