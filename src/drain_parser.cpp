// ============================================================================
// drain_parser.cpp
// ============================================================================
#include "drain_parser.h"
#include "log_record_object.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <sstream>
#include <iostream>
#include <chrono>
#include <numeric>
#include <memory>


// Folly includes
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/String.h>
#include <folly/Synchronized.h>
#include <folly/small_vector.h>
#include <spdlog/spdlog.h>

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

    // Allow initial +, -, or decimal point
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
        // If custom patterns exist, use them; else use default
        if (!custom_patterns_.empty()) {
            return custom_patterns_;
        }
        return default_patterns_;
    }

private:
    RegexCache() {
        // Some default patterns for trimming timestamps, brackets, etc.
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
    // We work on a temporary std::string to run regex searches
    std::string line_str(line);

    for (const auto& pattern : patterns) {
        std::smatch match;
        if (std::regex_search(line_str, match, pattern)) {
            size_t content_start = match.position() + match.length();
            if (content_start < line.size()) {
                return line.substr(content_start);
            }
        }
    }
    return line;
}

} // namespace detail


// ============================================================================
// DRAIN Structures
// ============================================================================

struct LogCluster {
    int id;
    std::string log_template;
    detail::TokenVector tokens;
    folly::F14FastSet<size_t> parameter_indices;

    explicit LogCluster(int id_, const detail::TokenVector& tok)
        : id(id_), tokens(tok)
    {
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
};

// ============================================================================
// DrainParserImpl
// ============================================================================
class DrainParserImpl {
public:
    // Internal DRAIN config
    struct DrainConfig {
        int depth;
        double similarity_threshold;
        int max_children;
    };

public:
    DrainParserImpl(int depth, double similarity_threshold, int max_children)
        : root_(std::make_shared<Node>()),
          cluster_id_counter_(0)
    {
        // Initialize our internal DRAIN config
        auto conf = drain_config_.wlock();
        conf->depth = depth;
        conf->similarity_threshold = similarity_threshold;
        conf->max_children = max_children;
    }

    LogRecordObject parse(std::string_view line, const DataLoaderConfig& user_cfg) {
        LogRecordObject record;
        record.body = std::string(line);

        // Preprocess and tokenize
        std::string_view content = detail::preprocess_log(line);
        auto tokens = detail::tokenize(content);

        // Match or create a cluster
        auto matched_cluster = match_log_message(tokens);

        record.template_str = matched_cluster->log_template;
        record.fields["cluster_id"] = std::to_string(matched_cluster->id);

        // Possibly extract more metadata from line or user_cfg
        extract_metadata(line, record, user_cfg);
        return record;
    }

    int get_cluster_id_for_log(std::string_view line) const {
        std::string_view content = detail::preprocess_log(line);
        auto tokens = detail::tokenize(content);
        auto matched_cluster = find_matching_cluster(tokens);
        return matched_cluster->id;
    }

    std::optional<int> get_cluster_id_from_record(const LogRecordObject& record) const {
        auto it = record.fields.find("cluster_id");
        if (it != record.fields.end()) {
            try {
                return std::stoi(it->second);
            } catch (const std::exception& e) {
                spdlog::error("Error converting cluster_id: {}", e.what());
            }
        }
        return std::nullopt;
    }

    void set_depth(int depth) {
        auto conf = drain_config_.wlock();
        conf->depth = depth;
    }

    void set_similarity_threshold(double threshold) {
        auto conf = drain_config_.wlock();
        conf->similarity_threshold = threshold;
    }

    std::optional<std::string> get_template_for_cluster_id(int cluster_id) const {
        auto tmpl_map = templates_.rlock();
        auto it = tmpl_map->find(cluster_id);
        if (it != tmpl_map->end()) {
            return it->second;
        }
        return std::nullopt;
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
        auto tmpl_map = templates_.rlock();
        for (const auto& [id, tmpl] : *tmpl_map) {
            result[id] = tmpl;
        }
        return result;
    }

private:
    /**
     * Match or create a LogCluster for the tokenized log line.
     */
    std::shared_ptr<LogCluster> match_log_message(const detail::TokenVector& tokens) {
        // If empty, treat as a special cluster
        if (tokens.empty()) {
            auto empty_cluster = std::make_shared<LogCluster>(
                cluster_id_counter_.fetch_add(1),
                detail::TokenVector{std::string_view("<EMPTY>")}
            );
            auto lock_t = templates_.wlock();
            (*lock_t)[empty_cluster->id] = empty_cluster->log_template;
            return empty_cluster;
        }

        // Lock for writing the root
        auto root_lock = root_.wlock();
        auto drain_conf = drain_config_.rlock();

        // 1) Match by token count at top level
        auto current_node = *root_lock;
        std::string length_key = std::to_string(tokens.size());

        auto iter = current_node->children.find(length_key);
        if (iter == current_node->children.end()) {
            // Create child node
            auto new_node = std::make_shared<Node>();
            current_node->children[length_key] = new_node;
            current_node = new_node;
        } else {
            current_node = iter->second;
        }

        // 2) Descend up to drain_conf->depth
        const int max_depth = std::min(drain_conf->depth, static_cast<int>(tokens.size()));
        for (int depth = 0; depth < max_depth; ++depth) {
            std::string_view token = tokens[depth];
            std::string token_key = detail::is_number(token) ? WILDCARD
                                                             : std::string(token);
            auto child_iter = current_node->children.find(token_key);
            if (child_iter == current_node->children.end()) {
                // Possibly fallback to wildcard if children is at capacity
                if ((int)current_node->children.size() < drain_conf->max_children) {
                    auto new_node = std::make_shared<Node>();
                    current_node->children[token_key] = new_node;
                    current_node = new_node;
                } else {
                    // Force to wildcard
                    token_key = WILDCARD;
                    auto w_iter = current_node->children.find(token_key);
                    if (w_iter == current_node->children.end()) {
                        auto new_node = std::make_shared<Node>();
                        current_node->children[token_key] = new_node;
                        current_node = new_node;
                    } else {
                        current_node = w_iter->second;
                    }
                }
            } else {
                current_node = child_iter->second;
            }
        }

        // 3) Among existing clusters, pick best match above threshold
        double max_similarity = -1.0;
        std::shared_ptr<LogCluster> matched_cluster = nullptr;

        for (auto& cluster : current_node->clusters) {
            double sim = calculate_similarity(cluster->tokens, tokens);
            if (sim > max_similarity && sim >= drain_conf->similarity_threshold) {
                max_similarity = sim;
                matched_cluster = cluster;
            }
        }

        // 4) If no match, create
        if (!matched_cluster) {
            matched_cluster = std::make_shared<LogCluster>(cluster_id_counter_.fetch_add(1), tokens);
            extract_parameters(tokens, matched_cluster);
            current_node->clusters.push_back(matched_cluster);
        } else {
            // Update the cluster's template if needed
            update_template(matched_cluster, tokens);
        }

        // Update the global template map
        {
            auto lock_t = templates_.wlock();
            (*lock_t)[matched_cluster->id] = matched_cluster->log_template;
        }

        return matched_cluster;
    }

    /**
     * Find existing cluster that best matches the tokens. Does NOT create a new cluster.
     */
    std::shared_ptr<LogCluster> find_matching_cluster(const detail::TokenVector& tokens) const {
        if (tokens.empty()) {
            return std::make_shared<LogCluster>(-1,
                detail::TokenVector{std::string_view("<EMPTY>")});
        }
        auto root_lock = root_.rlock();
        auto drain_conf = drain_config_.rlock();

        auto current_node = *root_lock;
        std::string length_key = std::to_string(tokens.size());

        auto iter = current_node->children.find(length_key);
        if (iter == current_node->children.end()) {
            // Not found
            return std::make_shared<LogCluster>(-1, tokens);
        }
        current_node = iter->second;

        const int max_depth = std::min(drain_conf->depth, static_cast<int>(tokens.size()));
        for (int depth = 0; depth < max_depth; ++depth) {
            std::string_view token = tokens[depth];
            std::string token_key = detail::is_number(token) ? WILDCARD
                                                             : std::string(token);

            auto child_iter = current_node->children.find(token_key);
            if (child_iter == current_node->children.end()) {
                // Try wildcard fallback
                child_iter = current_node->children.find(WILDCARD);
                if (child_iter == current_node->children.end()) {
                    return std::make_shared<LogCluster>(-1, tokens);
                }
            }
            current_node = child_iter->second;
        }

        // Now pick the best cluster that meets threshold
        double max_similarity = -1.0;
        std::shared_ptr<LogCluster> matched_cluster = nullptr;
        for (auto& cluster : current_node->clusters) {
            double sim = calculate_similarity(cluster->tokens, tokens);
            if (sim > max_similarity && sim >= drain_conf->similarity_threshold) {
                max_similarity = sim;
                matched_cluster = cluster;
            }
        }
        if (!matched_cluster) {
            // Not found
            return std::make_shared<LogCluster>(-1, tokens);
        }
        return matched_cluster;
    }

    /**
     * Compute similarity between two token vectors (simple example).
     */
    double calculate_similarity(const detail::TokenVector& t1,
                                const detail::TokenVector& t2) const
    {
        size_t common = 0;
        const size_t min_size = std::min(t1.size(), t2.size());
        for (size_t i = 0; i < min_size; ++i) {
            // If exact match or cluster token is wildcard
            if (t1[i] == t2[i] || t1[i] == WILDCARD) {
                ++common;
            }
        }
        return double(common) / double(std::max(t1.size(), t2.size()));
    }

    /**
     * Update a cluster template by merging in new tokens.
     */
    void update_template(const std::shared_ptr<LogCluster>& cluster,
                         const detail::TokenVector& tokens)
    {
        if (tokens.empty()) {
            return;
        }

        if (cluster->tokens.empty()) {
            // Just copy directly
            cluster->tokens = tokens;
            extract_parameters(tokens, cluster);
            cluster->update_template();
            return;
        }

        size_t min_sz = std::min(cluster->tokens.size(), tokens.size());
        for (size_t i = 0; i < min_sz; ++i) {
            if (cluster->tokens[i] != tokens[i] && cluster->tokens[i] != WILDCARD) {
                // If both numeric, wildcard them
                if (detail::is_number(cluster->tokens[i]) &&
                    detail::is_number(tokens[i]))
                {
                    cluster->tokens[i] = WILDCARD;
                    cluster->parameter_indices.insert(i);
                }
                else {
                    // Different tokens => wildcard
                    cluster->tokens[i] = WILDCARD;
                    cluster->parameter_indices.insert(i);
                }
            }
        }
        cluster->update_template();

        // Update the templates map
        auto lock_t = templates_.wlock();
        (*lock_t)[cluster->id] = cluster->log_template;
    }

    /**
     * Mark likely parameters (numbers, etc.) as wildcard/parameters.
     */
    void extract_parameters(const detail::TokenVector& tokens,
                            const std::shared_ptr<LogCluster>& cluster)
    {
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (detail::is_number(tokens[i])) {
                cluster->parameter_indices.insert(i);
            }
        }
    }

    /**
     * Example metadata extraction
     */
    void extract_metadata(std::string_view line,
                          LogRecordObject& record,
                          const DataLoaderConfig& /*unused*/)
    {
        try {
            // Just fill in a dummy source
            record.fields["source"] = "log";

            // If the line starts with something that looks like a timestamp
            if (!line.empty()) {
                size_t pos = line.find(' ');
                if (pos != std::string_view::npos) {
                    std::string_view maybe_ts = line.substr(0, pos);
                    if (maybe_ts.find(':') != std::string_view::npos ||
                        maybe_ts.find('-') != std::string_view::npos)
                    {
                        record.fields["detected_timestamp"] = std::string(maybe_ts);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Error extracting metadata: {}", e.what());
        }
    }

private:
    // A thread-safe structure for the DRAIN configuration:
    folly::Synchronized<DrainConfig> drain_config_{{
        /*depth=*/4, /*similarity_threshold=*/0.5, /*max_children=*/100
    }};

    // A thread-safe structure for the parse-tree root
    folly::Synchronized<std::shared_ptr<Node>> root_;

    // Cluster ID generator
    std::atomic<int> cluster_id_counter_;

    // Thread-safe map of id -> template
    folly::Synchronized<folly::F14FastMap<int, std::string>> templates_;
};

// ============================================================================
// DrainParser methods
// ============================================================================

DrainParser::DrainParser(const DataLoaderConfig& config,
                         int depth,
                         double similarity_threshold,
                         int max_children)
    : impl_(std::make_unique<DrainParserImpl>(depth, similarity_threshold, max_children)),
      user_config_(config)
{
}

DrainParser::~DrainParser() = default;

LogRecordObject DrainParser::parse_line(std::string_view line) {
    // We pass the user’s config in, in case metadata extraction needs it
    return impl_->parse(line, user_config_);
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

std::optional<std::string> DrainParser::get_template_for_cluster_id(int cluster_id) const {
    return impl_->get_template_for_cluster_id(cluster_id);
}

int DrainParser::get_cluster_id_for_log(std::string_view line) const {
    return impl_->get_cluster_id_for_log(line);
}

folly::F14FastMap<int, std::string> DrainParser::get_all_templates() const {
    return impl_->get_all_templates();
}

std::optional<int> DrainParser::get_cluster_id_from_record(const LogRecordObject& record) const {
    return impl_->get_cluster_id_from_record(record);
}

} // namespace logai
