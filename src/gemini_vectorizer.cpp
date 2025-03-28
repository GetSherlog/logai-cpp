/**
 * @file gemini_vectorizer.cpp
 * @brief Implementation of Gemini API vectorizer for log data
 */

#include "gemini_vectorizer.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iomanip>

namespace logai {

// Helper to encode URL components
std::string url_encode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return str;
    }
    
    char* encoded = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.length()));
    if (!encoded) {
        curl_easy_cleanup(curl);
        return str;
    }
    
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    
    return result;
}

GeminiVectorizer::GeminiVectorizer(const GeminiVectorizerConfig& config) {
    // Initialize configuration
    {
        auto cfg = config_.wlock();
        *cfg = config;
    }
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Create CURL handle using shared_ptr with custom deleter
    curl_ = std::shared_ptr<CURL>(
        curl_easy_init(),
        [](CURL* handle) { 
            if (handle) curl_easy_cleanup(handle); 
        }
    );
    
    if (!curl_) {
        throw std::runtime_error("Failed to initialize CURL");
    }
    
    // Set up common CURL options
    curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Add common headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, headers);
}

GeminiVectorizer::~GeminiVectorizer() {
    // Reset the curl handle (shared_ptr will automatically clean up)
    curl_.reset();
    
    // Clean up CURL global resources
    curl_global_cleanup();
}

std::optional<std::vector<float>> GeminiVectorizer::get_embedding(const std::string& text) {
    // Check in the cache first
    {
        auto cache = embedding_cache_.rlock();
        auto it = cache->find(text);
        if (it != cache->end()) {
            return it->second;
        }
    }
    
    // Cache miss, call the API
    GeminiVectorizerConfig config_copy;
    {
        auto config = config_.rlock();
        config_copy = *config;
    }
    
    // Get CURL handle
    CURL* curl_handle = curl_.get();
    
    if (!curl_handle) {
        spdlog::error("CURL not initialized");
        return std::nullopt;
    }
    
    std::string api_key = get_api_key();
    if (api_key.empty()) {
        spdlog::error("Gemini API key not found");
        return std::nullopt;
    }
    
    std::string url = build_request_url();
    std::string payload = build_request_payload(text);
    
    // Use the curl handle for this request
    CURL* request_curl = curl_handle;
    
    std::optional<std::vector<float>> result;
    
    // Set request URL
    curl_easy_setopt(request_curl, CURLOPT_URL, url.c_str());
    
    // Set request method to POST
    curl_easy_setopt(request_curl, CURLOPT_POST, 1L);
    
    // Set request payload
    curl_easy_setopt(request_curl, CURLOPT_POSTFIELDS, payload.c_str());
    
    // Set callback for response
    std::string response;
    curl_easy_setopt(request_curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(request_curl, CURLOPT_WRITEDATA, &response);
    
    // Perform request
    CURLcode res = curl_easy_perform(request_curl);
        
        if (res != CURLE_OK) {
            spdlog::error("CURL request failed: {}", curl_easy_strerror(res));
            return std::nullopt;
        }
        
    // Parse response
    try {
        nlohmann::json json_response = nlohmann::json::parse(response);
        
        if (json_response.contains("embedding")) {
            std::vector<float> embedding;
            
            if (json_response["embedding"].contains("values")) {
                embedding = json_response["embedding"]["values"].get<std::vector<float>>();
            } else if (json_response["embedding"].is_array()) {
                embedding = json_response["embedding"].get<std::vector<float>>();
            } else {
                spdlog::error("Invalid embedding format in response");
                return std::nullopt;
            }
            
            result = embedding;
        } else if (json_response.contains("embeddings")) {
            std::vector<float> embedding;
            
            if (json_response["embeddings"].is_array() && !json_response["embeddings"].empty()) {
                embedding = json_response["embeddings"][0].get<std::vector<float>>();
                result = embedding;
            }
        } else if (json_response.contains("error")) {
            spdlog::error("API error: {}", json_response["error"].dump());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse JSON response: {}", e.what());
    }
    
    // If we got a result, add it to the cache
    if (result) {
        auto cache = embedding_cache_.wlock();
        
        // Check cache capacity
        if (cache->size() >= config_copy.cache_capacity) {
            // Simple strategy: remove a random entry
            auto it = cache->begin();
            cache->erase(it);
        }
        
        (*cache)[text] = *result;
    }
    
    return result;
}

bool GeminiVectorizer::is_valid() {
    auto config = config_.rlock();
    if (config->api_key.empty() && !std::getenv(config->api_key_env_var.c_str())) {
        return false;
    }
    
    auto test_embedding = get_embedding("Test message");
    return test_embedding.has_value();
}

void GeminiVectorizer::set_api_key(const std::string& api_key) {
    {
        auto config = config_.wlock();
        config->api_key = api_key;
        config->use_env_api_key = false;
    }
    
    {
        auto cache = embedding_cache_.wlock();
        cache->clear();
    }
}

void GeminiVectorizer::set_model_name(const std::string& model_name) {
    {
        auto config = config_.wlock();
        config->model_name = model_name;
    }
    
    {
        auto cache = embedding_cache_.wlock();
        cache->clear();
    }
}

std::string GeminiVectorizer::get_api_key() const {
    GeminiVectorizerConfig config_copy;
    {
        auto config = config_.rlock();
        config_copy = *config;
    }
    
    if (!config_copy.use_env_api_key) {
        return config_copy.api_key;
    }
    
    const char* env_api_key = std::getenv(config_copy.api_key_env_var.c_str());
    return env_api_key ? env_api_key : "";
}

std::string GeminiVectorizer::build_request_url() const {
    std::string url;
    std::string model;
    {
        auto config = config_.rlock();
        url = std::string("https://generativelanguage.googleapis.com") + "/v1/models/" + config->model_name + ":embedContent";
        model = config->model_name;
    }
    
    std::string encoded_model = url_encode(model);
    return url;
}

std::string GeminiVectorizer::build_request_payload(const std::string& text) const {
    nlohmann::json payload;
    {
        auto config = config_.rlock();
        payload["model"] = config->model_name;
    }
    
    nlohmann::json content;
    content["parts"][0]["text"] = text;
    payload["contents"].push_back(content);
    
    return payload.dump();
}

size_t GeminiVectorizer::write_callback(void* contents, size_t size, size_t nmemb, std::string* response) {
    size_t realsize = size * nmemb;
    response->append((char*)contents, realsize);
    return realsize;
}

} // namespace logai