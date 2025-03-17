#include "drain_parser.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <iostream>
#include <chrono>
#include <numeric>

// Folly includes
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/String.h>
#include <folly/Synchronized.h>
#include <folly/small_vector.h>
#include <spdlog/spdlog.h>

// Consider CTRE for regex if available
#include <regex>

namespace logai {

constexpr char WILDCARD[] = "<*>";

namespace detail {

using TokenVector = folly::small_vector<std::string_view, 32>;

TokenVector tokenize(std::string_view str, char delimiter = ' ') {
    TokenVector tokens;
    if (str.empty()) {
        return tokens;
    }
    
    try {
        folly::split(delimiter, str, tokens);
    } catch (const std::exception& e) {
        spdlog::error("Error in tokenize function: {}", e.what());
    }
    
    return tokens;
}

bool is_number(std::string_view str) {
    if (str.empty()) return false;
    
    if (str.size() == 1) return std::isdigit(str[0]);
    
    if (!std::isdigit(str[0]) && str[0] != '-' && str[0] != '+' && str[0] != '.') {
        return false;
    }
    
    bool has_dot = (str[0] == '.');
    
    for (size_t i = 1; i < str.size(); ++i) {
        char c = str[i];
        if (c == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (!std::isdigit(c)) {
            return false;
        }
    }
    
    return true;
}

class RegexCache {
public:
    static RegexCache& instance() {
        static RegexCache cache;
        return cache;
    }
    
    const std::vector<std::regex>& get_default_patterns() const {
        return default_patterns_;
    }
    
    void set_custom_patterns(std::vector<std::regex> patterns) {
        custom_patterns_ = std::move(patterns);
    }
    
    const std::vector<std::regex>& get_patterns() const {
        if (!custom_patterns_.empty()) {
            return custom_patterns_;
        }
        return default_patterns_;
    }
    
private:
    RegexCache() {
        default_patterns_.emplace_back(R"(^\[.*?\]\s*)");
        default_patterns_.emplace_back(R"(^\d{4}[-/]\d{1,2}[-/]\d{1,2}\s+\d{1,2}:\d{1,2}:\d{1,2}(?:\.\d+)?\s+)");
        default_patterns_.emplace_back(R"(^\d{1,2}:\d{1,2}:\d{1,2}(?:\.\d+)?\s+)");
        default_patterns_.emplace_back(R"((?i)^\s*(?:ERROR|WARN(?:ING)?|INFO|DEBUG|TRACE|FATAL)\s*:?\s*)");
        default_patterns_.emplace_back(R"(^\w+\s+\w+\s+\d+\s+\d{2}:\d{2}:\d{2}\s+\d{4}\s+)");
        default_patterns_.emplace_back(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?\s+)");
    }
    
    std::vector<std::regex> default_patterns_;
    std::vector<std::regex> custom_patterns_;
};

// Preprocess log using regex patterns
std::string_view preprocess_log(std::string_view line) {
    const auto& patterns = RegexCache::instance().get_patterns();
    std::string line_str(line);
    
    for (const auto& pattern : patterns) {
        std::smatch match;
        if (std::regex_search(line_str, match, pattern)) {
            size_t content_start = match.position() + match.length();
            if (content_start < line.length()) {
                return line.substr(content_start);
            }
        }
    }
    return line;
}

} // namespace detail

// DRAIN structures
struct LogCluster {
    int id;
    std::string log_template;
    detail::TokenVector tokens;
    folly::F14FastSet<size_t> parameter_indices;
    
    explicit LogCluster(int id, const detail::TokenVector& tok) 
        : id(id), tokens(tok) {
        update_template();
    }
    
    void update_template() {
        log_template.clear();
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) log_template.push_back(' ');
            log_template.append(tokens[i].data(), tokens[i].size());
        }
    }
};

struct Node {
    folly::F14FastMap<std::string, std::shared_ptr<Node>> children;
    folly::small_vector<std::shared_ptr<LogCluster>, 8> clusters;
    
    Node() = default;
};

// Implementation class for DrainParser
class DrainParserImpl {
public:
    DrainParserImpl(int depth, double similarity_threshold, int max_children)
        : depth_(depth), 
          similarity_threshold_(similarity_threshold), 
          max_children_(max_children), 
          root_(std::make_shared<Node>()),
          cluster_id_counter_(0) 
    {
        // Initialize configuration
        auto config = config_.wlock();
        config->depth = depth;
        config->similarity_threshold = similarity_threshold;
        config->max_children = max_children;
    }
    
    LogRecordObject parse(std::string_view line, const DataLoaderConfig& config) {
        LogRecordObject record;
        record.body = std::string(line);
        
        std::string_view content = detail::preprocess_log(line);
        auto tokens = detail::tokenize(content);
        
        auto matched_cluster = match_log_message(tokens);
        
        record.template_str = matched_cluster->log_template;
        record.attributes["cluster_id"] = std::to_string(matched_cluster->id);
    
        extract_metadata(line, record, config);
        
        return record;
    }
    
    int get_cluster_id_for_log(std::string_view line) const {
        std::string_view content = detail::preprocess_log(line);
        auto tokens = detail::tokenize(content);
        auto matched_cluster = find_matching_cluster(tokens);
        return matched_cluster->id;
    }
    
    std::optional<int> get_cluster_id_from_record(const LogRecordObject& record) const {
        auto it = record.attributes.find("cluster_id");
        if (it != record.attributes.end()) {
            try {
                return std::stoi(it->second);
            } catch (const std::exception& e) {
                spdlog::error("Error converting cluster_id: {}", e.what());
            }
        }
        return std::nullopt;
    }
    
    void set_depth(int depth) {
        auto config = config_.wlock();
        config->depth = depth;
    }
    
    void set_similarity_threshold(double threshold) {
        auto config = config_.wlock();
        config->similarity_threshold = threshold;
    }
    
    std::optional<std::string> get_template_for_cluster_id(int cluster_id) const {
        auto templates = templates_.rlock();
        auto it = templates->find(cluster_id);
        if (it == templates->end()) {
            return std::nullopt;
        }
        return it->second;
    }
    
    void set_preprocess_patterns(const std::vector<std::string>& pattern_strings) {
        std::vector<std::regex> patterns;
        patterns.reserve(pattern_strings.size());
        
        for (const auto& pattern_str : pattern_strings) {
            try {
                patterns.emplace_back(pattern_str);
            } catch (const std::regex_error& e) {
                spdlog::error("Invalid regex pattern: {} - {}", pattern_str, e.what());
            }
        }
        
        detail::RegexCache::instance().set_custom_patterns(std::move(patterns));
    }
    
    folly::F14FastMap<int, std::string> get_all_templates() const {
        folly::F14FastMap<int, std::string> result;
        auto templates = templates_.rlock();
        for (const auto& [id, tmpl] : *templates) {
            result.emplace(id, tmpl);
        }
        return result;
    }
    
private:
    // Thread-safe configuration
    struct Config {
        int depth;
        double similarity_threshold;
        int max_children;
    };
    
    folly::Synchronized<Config> config_{{
        .depth = 0,
        .similarity_threshold = 0.0,
        .max_children = 0
    }};
    
    folly::Synchronized<std::shared_ptr<Node>> root_;
    
    std::atomic<int> cluster_id_counter_;
    
    folly::Synchronized<folly::F14FastMap<int, std::string>> templates_;
    
    int depth_;
    double similarity_threshold_;
    int max_children_;
    
    std::shared_ptr<LogCluster> match_log_message(const detail::TokenVector& tokens) {
        // Handle empty tokens
        if (tokens.empty()) {
            auto empty_cluster = std::make_shared<LogCluster>(
                cluster_id_counter_++, detail::TokenVector{std::string_view("<EMPTY>")}
            );
            
            // Cache the template
            auto templates = templates_.wlock();
            (*templates)[empty_cluster->id] = empty_cluster->log_template;
            
            return empty_cluster;
        }
        
        auto root_locked = root_.wlock();
        auto config = config_.rlock();
        auto current_node = *root_locked;
        
        std::string length_key = std::to_string(tokens.size());
        
        if (!current_node->children.contains(length_key)) {
            current_node->children[length_key] = std::make_shared<Node>();
        }
        current_node = current_node->children[length_key];
        
        const int max_depth = std::min(config->depth, static_cast<int>(tokens.size()));
        for (int depth = 0; depth < max_depth; ++depth) {
            std::string_view token = tokens[depth];
            std::string token_key;
            
            if (detail::is_number(token)) {
                token_key = WILDCARD;
            } else {
                token_key = std::string(token);
            }
            
            if (!current_node->children.contains(token_key)) {
                if (current_node->children.size() < config->max_children) {
                    current_node->children[token_key] = std::make_shared<Node>();
                } else {
                    token_key = WILDCARD;
                    if (!current_node->children.contains(token_key)) {
                        current_node->children[token_key] = std::make_shared<Node>();
                    }
                }
            }
            
            current_node = current_node->children[token_key];
        }
        
        double max_similarity = -1.0;
        std::shared_ptr<LogCluster> matched_cluster = nullptr;
        
        for (const auto& cluster : current_node->clusters) {
            double similarity = calculate_similarity(cluster->tokens, tokens);
            
            if (similarity > max_similarity && similarity >= config->similarity_threshold) {
                max_similarity = similarity;
                matched_cluster = cluster;
            }
        }
        
        if (!matched_cluster) {
            matched_cluster = std::make_shared<LogCluster>(cluster_id_counter_++, tokens);
            extract_parameters(tokens, matched_cluster);
            current_node->clusters.push_back(matched_cluster);
        } else {
            update_template(matched_cluster, tokens);
        }
        
        auto templates = templates_.wlock();
        (*templates)[matched_cluster->id] = matched_cluster->log_template;
        
        return matched_cluster;
    }
    
    std::shared_ptr<LogCluster> find_matching_cluster(const detail::TokenVector& tokens) const {
        if (tokens.empty()) {
            return std::make_shared<LogCluster>(-1, detail::TokenVector{std::string_view("<EMPTY>")});
        }
        
        auto root_locked = root_.rlock();
        auto config = config_.rlock();
        auto current_node = *root_locked;
        
        std::string length_key = std::to_string(tokens.size());
        
        if (!current_node->children.contains(length_key)) {
            return std::make_shared<LogCluster>(-1, tokens);
        }
        current_node = current_node->children.at(length_key);
        
        const int max_depth = std::min(config->depth, static_cast<int>(tokens.size()));
        for (int depth = 0; depth < max_depth; ++depth) {
            std::string_view token = tokens[depth];
            std::string token_key;
            
            if (detail::is_number(token)) {
                token_key = WILDCARD;
            } else {
                token_key = std::string(token);
            }
            
            // Try to find the token in children
            if (!current_node->children.contains(token_key)) {
                // Try wildcard as fallback
                token_key = WILDCARD;
                if (!current_node->children.contains(token_key)) {
                    // No matching path, return a default cluster
                    return std::make_shared<LogCluster>(-1, tokens);
                }
            }
            
            // Move to the child node
            current_node = current_node->children.at(token_key);
        }
        
        // Find best matching cluster
        double max_similarity = -1.0;
        std::shared_ptr<LogCluster> matched_cluster = nullptr;
        
        // Try to find the best match from existing clusters
        for (const auto& cluster : current_node->clusters) {
            double similarity = calculate_similarity(cluster->tokens, tokens);
            
            if (similarity > max_similarity && similarity >= config->similarity_threshold) {
                max_similarity = similarity;
                matched_cluster = cluster;
            }
        }
        
        // If no match found, return a temporary cluster
        if (!matched_cluster) {
            return std::make_shared<LogCluster>(-1, tokens);
        }
        
        return matched_cluster;
    }
    
    double calculate_similarity(const detail::TokenVector& tokens1, const detail::TokenVector& tokens2) const {
        // Calculate similarity based on common tokens
        size_t common_tokens = 0;
        const size_t min_size = std::min(tokens1.size(), tokens2.size());
        
        for (size_t i = 0; i < min_size; ++i) {
            if (tokens1[i] == tokens2[i] || tokens1[i] == WILDCARD) {
                ++common_tokens;
            }
        }
        
        // Calculate normalized similarity
        return static_cast<double>(common_tokens) / std::max(tokens1.size(), tokens2.size());
    }
    
    void update_template(std::shared_ptr<LogCluster> cluster, const detail::TokenVector& tokens) {
        if (tokens.empty()) {
            return;
        }
        
        // First time seeing this cluster? Initialize with the tokens
        if (cluster->tokens.empty()) {
            cluster->tokens = tokens;
            extract_parameters(tokens, cluster);
            cluster->update_template();
            return;
        }
        
        // Update template tokens
        const size_t min_size = std::min(cluster->tokens.size(), tokens.size());
        
        // Check for token mismatches and convert to wildcards
        for (size_t i = 0; i < min_size; ++i) {
            if (cluster->tokens[i] != tokens[i] && cluster->tokens[i] != WILDCARD) {
                if (detail::is_number(cluster->tokens[i]) && detail::is_number(tokens[i])) {
                    // Both are numbers, convert to wildcard
                    cluster->tokens[i] = WILDCARD;
                    cluster->parameter_indices.insert(i);
                } else if (cluster->tokens[i] != tokens[i]) {
                    // Different tokens, convert to wildcard
                    cluster->tokens[i] = WILDCARD;
                    cluster->parameter_indices.insert(i);
                }
            }
        }
        
        // Update template string
        cluster->update_template();
        
        // Update cache
        auto templates = templates_.wlock();
        (*templates)[cluster->id] = cluster->log_template;
    }
    
    void extract_parameters(const detail::TokenVector& tokens, std::shared_ptr<LogCluster> cluster) {
        // Mark tokens that are likely parameters (e.g., numbers, hex, etc.)
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (detail::is_number(tokens[i])) {
                cluster->parameter_indices.insert(i);
            }
        }
    }
    
    void extract_metadata(std::string_view line, LogRecordObject& record, const DataLoaderConfig& config) {
        try {
            // Add some basic metadata
            record.attributes["source"] = "log";
            
            // Basic timestamp detection
            if (!line.empty()) {
                size_t timestamp_end = line.find(' ');
                if (timestamp_end != std::string_view::npos && timestamp_end > 0) {
                    std::string_view timestamp_str = line.substr(0, timestamp_end);
                    
                    if (timestamp_str.find(':') != std::string_view::npos || 
                        timestamp_str.find('-') != std::string_view::npos) {
                        record.attributes["detected_timestamp"] = std::string(timestamp_str);
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Error extracting metadata: {}", e.what());
        }
    }
};

// Public API implementation

DrainParser::DrainParser(const DataLoaderConfig& config, int depth, double similarity_threshold, int max_children)
    : impl_(std::make_unique<DrainParserImpl>(depth, similarity_threshold, max_children)), 
      config_(config) {
}

DrainParser::~DrainParser() = default;

LogRecordObject DrainParser::parse_line(std::string_view line) {
    return impl_->parse(line, config_);
}

void DrainParser::setDepth(int depth) {
    impl_->set_depth(depth);
}

void DrainParser::setSimilarityThreshold(double threshold) {
    impl_->set_similarity_threshold(threshold);
}

void DrainParser::set_preprocess_patterns(const std::vector<std::string>& pattern_strings) {
    impl_->set_preprocess_patterns(pattern_strings);
}

folly::F14FastMap<int, std::string> DrainParser::get_all_templates() const {
    return impl_->get_all_templates();
}

int DrainParser::get_cluster_id_for_log(std::string_view line) const {
    return impl_->get_cluster_id_for_log(line);
}

std::optional<int> DrainParser::get_cluster_id_from_record(const LogRecordObject& record) const {
    return impl_->get_cluster_id_from_record(record);
}

std::optional<std::string> DrainParser::get_template_for_cluster_id(const int cluster_id) const {
    return impl_->get_template_for_cluster_id(cluster_id);
}

} // namespace logai