#include "template_store.h"
#include <algorithm>
#include <fstream>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace logai {

namespace detail {
    // Efficient similarity calculation helper
    float calculate_cosine_similarity(const std::vector<float>& v1, const std::vector<float>& v2) {
        if (v1.empty() || v2.empty() || v1.size() != v2.size()) {
            return 0.0f;
        }
        
        float dot_product = 0.0f;
        float norm_v1 = 0.0f;
        float norm_v2 = 0.0f;
        
        // Direct pointer access for better performance
        const float* p1 = v1.data();
        const float* p2 = v2.data();
        const size_t size = v1.size();
        
        for (size_t i = 0; i < size; ++i) {
            dot_product += p1[i] * p2[i];
            norm_v1 += p1[i] * p1[i];
            norm_v2 += p2[i] * p2[i];
        }
        
        if (norm_v1 <= 0.0f || norm_v2 <= 0.0f) {
            return 0.0f;
        }
        
        return dot_product / (std::sqrt(norm_v1) * std::sqrt(norm_v2));
    }
} // namespace detail

TemplateStore::TemplateStore() {
    // Initialize vectorizer to nullptr (handled by the Synchronized wrapper)
}

TemplateStore::~TemplateStore() = default;

bool TemplateStore::add_template(int template_id, const std::string& template_str, const LogRecordObject& log) {
    try {
        // Get embedding first, so we don't have to lock multiple structures at once
        auto embedding_opt = get_embedding(template_str);
        
        // Store the template
        {
            auto templates = templates_.wlock();
            (*templates)[template_id] = template_str;
        }
        
        // Store the log
        {
            auto logs = template_logs_.wlock();
            if (logs->find(template_id) == logs->end()) {
                (*logs)[template_id] = std::vector<LogRecordObject>{log};
            } else {
                (*logs)[template_id].push_back(log);
            }
        }
        
        // Store the embedding
        if (embedding_opt) {
            auto embeddings = embeddings_.wlock();
            (*embeddings)[template_id] = std::move(*embedding_opt);
        }
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Error adding template: {}", e.what());
        return false;
    }
}

std::vector<std::pair<int, float>> TemplateStore::search(const std::string& query, int top_k) {
    std::vector<std::pair<int, float>> results;
    
    try {
        // Get embedding for query
        auto query_embedding = get_embedding(query);
        if (!query_embedding) {
            spdlog::error("Could not generate embedding for query");
            return results;
        }
        
        // Create vector of (template_id, similarity) pairs
        std::vector<std::pair<int, float>> similarities;
        similarities.reserve(50);  // Avoid too many reallocations
        
        {
            // Get read locks for templates and embeddings
            auto templates = templates_.rlock();
            auto embeddings = embeddings_.rlock();
            
            for (const auto& [id, embedding] : *embeddings) {
                float similarity = cosine_similarity(*query_embedding, embedding);
                similarities.emplace_back(id, similarity);
            }
        }
        
        // Sort by similarity (descending)
        std::sort(similarities.begin(), similarities.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // Limit results to top_k
        size_t count = std::min(static_cast<size_t>(top_k), similarities.size());
        results.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            results.push_back(similarities[i]);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Error searching templates: {}", e.what());
    }
    
    return results;
}

std::optional<std::string> TemplateStore::get_template(int template_id) const {
    auto templates = templates_.rlock();
    auto it = templates->find(template_id);
    if (it != templates->end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::vector<LogRecordObject>> TemplateStore::get_logs(int template_id) const {
    auto logs = template_logs_.rlock();
    auto it = logs->find(template_id);
    if (it != logs->end()) {
        return it->second;
    }
    return std::nullopt;
}

bool TemplateStore::init_vectorizer(const GeminiVectorizerConfig& config) {
    try {
        // Create the vectorizer
        auto new_vectorizer = std::make_shared<GeminiVectorizer>(config);
        
        // Check if vectorizer is valid (API key works)
        if (!new_vectorizer->is_valid()) {
            spdlog::error("Gemini vectorizer initialization failed: Invalid API key or connection issue");
            return false;
        }
        
        // Update the vectorizer
        {
            auto vectorizer = vectorizer_.wlock();
            *vectorizer = std::move(new_vectorizer);
        }
        
        // Clear the embedding cache
        {
            auto cache = embedding_cache_.wlock();
            cache->clear();
        }
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Error initializing Gemini vectorizer: {}", e.what());
        return false;
    }
}

bool TemplateStore::save(const std::string& path) const {
    try {
        json j;
        
        // Get read locks and copy data to avoid holding locks during file I/O
        folly::F14FastMap<int, std::string> templates_copy;
        folly::F14FastMap<int, std::vector<float>> embeddings_copy;
        
        {
            auto templates = templates_.rlock();
            templates_copy = *templates;
        }
        
        {
            auto embeddings = embeddings_.rlock();
            embeddings_copy = *embeddings;
        }
        
        // Save templates
        for (const auto& [id, tmpl] : templates_copy) {
            j["templates"][std::to_string(id)] = tmpl;
        }
        
        // Save embeddings
        for (const auto& [id, emb] : embeddings_copy) {
            j["embeddings"][std::to_string(id)] = emb;
        }
        
        // Save to file
        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Could not open file for writing: {}", path);
            return false;
        }
        
        file << j.dump(4);
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Error saving template store: {}", e.what());
        return false;
    }
}

bool TemplateStore::load(const std::string& path) {
    try {
        // Open file
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::error("Could not open file: {}", path);
            return false;
        }
        
        // Parse JSON
        json j;
        file >> j;
        file.close();
        
        // Prepare new data
        folly::F14FastMap<int, std::string> new_templates;
        folly::F14FastMap<int, std::vector<float>> new_embeddings;
        
        // Load templates
        if (j.contains("templates")) {
            for (const auto& [id_str, tmpl] : j["templates"].items()) {
                int id = std::stoi(id_str);
                new_templates[id] = tmpl.get<std::string>();
            }
        }
        
        // Load embeddings
        if (j.contains("embeddings")) {
            for (const auto& [id_str, emb] : j["embeddings"].items()) {
                int id = std::stoi(id_str);
                new_embeddings[id] = emb.get<std::vector<float>>();
            }
        }
        
        // Update the store with read locks
        {
            auto templates = templates_.wlock();
            *templates = std::move(new_templates);
        }
        
        {
            auto embeddings = embeddings_.wlock();
            *embeddings = std::move(new_embeddings);
        }
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Error loading template store: {}", e.what());
        return false;
    }
}

size_t TemplateStore::size() const {
    auto templates = templates_.rlock();
    return templates->size();
}

std::optional<std::vector<float>> TemplateStore::get_embedding(const std::string& text) {
    // Check if we have the embedding cached
    {
        auto cache = embedding_cache_.rlock();
        auto it = cache->find(text);
        if (it != cache->end()) {
            return it->second;
        }
    }
    
    // Check if we have a Gemini vectorizer
    std::shared_ptr<GeminiVectorizer> vec_copy;
    {
        auto vectorizer = vectorizer_.rlock();
        vec_copy = *vectorizer;
    }
    
    if (!vec_copy) {
        return std::nullopt;
    }
    
    try {
        // Get embedding from Gemini vectorizer
        auto embedding = vec_copy->get_embedding(text);
        
        // Cache the result if successful
        if (embedding) {
            auto cache = embedding_cache_.wlock();
            (*cache)[text] = *embedding;
        }
        
        return embedding;
    } catch (const std::exception& e) {
        spdlog::error("Error generating embedding: {}", e.what());
        return std::nullopt;
    }
}

float TemplateStore::cosine_similarity(const std::vector<float>& v1, const std::vector<float>& v2) const {
    return detail::calculate_cosine_similarity(v1, v2);
}

} // namespace logai