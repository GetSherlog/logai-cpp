/**
 * @file gemini_vectorizer.h
 * @brief C++ implementation of Gemini embedding API vectorizer for log data
 */
#pragma once
#include "std_includes.h"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <folly/container/F14Map.h>
#include <folly/Synchronized.h>

namespace logai {

/**
 * @brief Configuration for the GeminiVectorizer
 */
struct GeminiVectorizerConfig {
    std::string model_name = "gemini-embedding-exp-03-07"; ///< Name of the Gemini model to use
    std::string api_key = "";                              ///< API key for Gemini API
    bool use_env_api_key = true;                           ///< Whether to use API key from environment variable
    std::string api_key_env_var = "GEMINI_API_KEY";       ///< Environment variable name for API key
    int embedding_dim = 768;                              ///< Dimension of the embeddings
    int cache_capacity = 1000;                            ///< Maximum number of entries in the embedding cache
};

/**
 * @brief Thread-safe C++ implementation of Gemini API vectorizer for log data
 */
class GeminiVectorizer {
public:
    /**
     * @brief Construct a new GeminiVectorizer object
     *
     * @param config Configuration for the vectorizer
     */
    explicit GeminiVectorizer(const GeminiVectorizerConfig& config);
    
    /**
     * @brief Destructor - cleans up CURL resources
     */
    ~GeminiVectorizer();
    
    /**
     * @brief Get embedding for a text using Gemini API (thread-safe)
     *
     * @param text Text to generate embedding for
     * @return std::optional<std::vector<float>> Optional embedding vector
     */
    std::optional<std::vector<float>> get_embedding(const std::string& text);
    
    /**
     * @brief Check if API key is valid (thread-safe)
     *
     * @return true If API key is valid and API is accessible
     * @return false If API key is invalid or API is not accessible
     */
    bool is_valid();
    
    /**
     * @brief Set the API key directly (thread-safe)
     *
     * @param api_key API key to use
     */
    void set_api_key(const std::string& api_key);
    
    /**
     * @brief Set the model name (thread-safe)
     *
     * @param model_name Model name to use
     */
    void set_model_name(const std::string& model_name);
    
private:
    // Thread-safe configuration
    folly::Synchronized<GeminiVectorizerConfig> config_;
    
    // CURL handle for API requests (not thread-safe, protected by mutex)
    std::shared_ptr<CURL> curl_;
    
    // Embedding cache with thread-safe access
    folly::Synchronized<folly::F14FastMap<std::string, std::vector<float>>> embedding_cache_;
    
    // Private helper methods
    std::string get_api_key() const;
    std::string build_request_url() const;
    std::string build_request_payload(const std::string& text) const;
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* response);
};

} // namespace logai