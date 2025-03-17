#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "log_record.h"
#include "gemini_vectorizer.h"
#include <folly/container/F14Map.h>
#include <folly/Synchronized.h>

namespace logai {

/**
 * TemplateStore - A thread-safe class to store and search log templates using embeddings
 *
 * This class stores log templates and their embeddings, and provides
 * functionality to search for templates similar to a given query.
 */
class TemplateStore {
public:
    /**
     * Constructor for TemplateStore
     */
    TemplateStore();
    
    /**
     * Destructor
     */
    ~TemplateStore();
    
    /**
     * Add a template to the store
     *
     * @param template_id The template ID (usually cluster ID)
     * @param template_str The template string to store
     * @param log The original log record object associated with this template
     * @return true if successfully added, false otherwise
     */
    bool add_template(int template_id, const std::string& template_str, const LogRecordObject& log);
    
    /**
     * Search for templates similar to a query
     *
     * @param query The query string to search for
     * @param top_k The number of results to return (default: 10)
     * @return A vector of pairs containing template IDs and their similarity scores
     */
    std::vector<std::pair<int, float>> search(const std::string& query, int top_k = 10);
    
    /**
     * Get a template by ID
     *
     * @param template_id The template ID to retrieve
     * @return The template string, or nullopt if not found
     */
    std::optional<std::string> get_template(int template_id) const;
    
    /**
     * Get logs for a template
     *
     * @param template_id The template ID to retrieve logs for
     * @return A vector of log record objects associated with this template, or nullopt if not found
     */
    std::optional<std::vector<LogRecordObject>> get_logs(int template_id) const;
    
    /**
     * Initialize the Gemini vectorizer
     *
     * @param config The Gemini vectorizer configuration
     * @return true if successful, false otherwise
     */
    bool init_vectorizer(const GeminiVectorizerConfig& config);
    
    /**
     * Save the template store to disk
     *
     * @param path The path to save to
     * @return true if successful, false otherwise
     */
    bool save(const std::string& path) const;
    
    /**
     * Load the template store from disk
     *
     * @param path The path to load from
     * @return true if successful, false otherwise
     */
    bool load(const std::string& path);
    
    /**
     * Get the number of templates in the store
     *
     * @return The number of templates
     */
    size_t size() const;
    
private:
    // Thread-safe data structures using Folly's Synchronized
    
    // Map of template ID to template string
    folly::Synchronized<folly::F14FastMap<int, std::string>> templates_;
    
    // Map of template ID to logs
    folly::Synchronized<folly::F14FastMap<int, std::vector<LogRecordObject>>> template_logs_;
    
    // Map of template ID to embedding
    folly::Synchronized<folly::F14FastMap<int, std::vector<float>>> embeddings_;
    
    // Vectorizer for creating embeddings
    folly::Synchronized<std::shared_ptr<GeminiVectorizer>> vectorizer_;
    
    // Cache for vectorizer to avoid repeated tokenization
    folly::Synchronized<folly::F14FastMap<std::string, std::vector<float>>> embedding_cache_;
    
    // Private helper methods
    std::optional<std::vector<float>> get_embedding(const std::string& text);
    float cosine_similarity(const std::vector<float>& v1, const std::vector<float>& v2) const;
};

} // namespace logai