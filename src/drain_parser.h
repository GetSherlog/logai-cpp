// ============================================================================
// drain_parser.h
// ============================================================================
#pragma once

#include "log_parser.h"
#include "data_loader_config.h"

#include <string>
#include <vector>
#include <unordered_set>
#include <optional>
#include <memory>

namespace logai {

// Forward declaration of the implementation class
class DrainParserImpl;

// String pool for interning common strings to reduce memory usage
class StringPool {
public:
    std::string_view intern(std::string_view str) {
        if (str.empty()) {
            static const std::string empty;
            return empty;
        }
        auto it = pool_.find(std::string(str));
        if (it != pool_.end()) {
            return *it;
        }
        auto result = pool_.insert(std::string(str));
        return *result.first;
    }

    size_t size() const {
        return pool_.size();
    }

private:
    std::unordered_set<std::string> pool_;
};

/**
 * DRAIN log parser - A high-performance implementation of the DRAIN log parsing algorithm.
 */
class DrainParser : public LogParser {
public:
    /**
     * Constructor for DrainParser
     * 
     * @param config The data loader configuration
     * @param depth The maximum depth of the parse tree (default: 4)
     * @param similarity_threshold The similarity threshold for grouping logs (default: 0.5)
     * @param max_children The maximum number of children per node (default: 100)
     */
    DrainParser(const DataLoaderConfig& config,
                int depth = 4,
                double similarity_threshold = 0.5,
                int max_children = 100);

    /**
     * Destructor
     */
    ~DrainParser() noexcept;

    /**
     * Parse a log line using the DRAIN algorithm
     */
    LogRecordObject parse_line(const std::string& line) override;

    /**
     * Set the maximum depth of the parse tree
     */
    void setDepth(int depth);

    /**
     * Set the similarity threshold for grouping logs
     */
    void setSimilarityThreshold(double threshold);

    /**
     * Set custom regex patterns for log preprocessing
     */
    void set_preprocess_patterns(const std::vector<std::string>& pattern_strings);

    /**
     * Retrieve the template for a given cluster ID
     */
    std::optional<std::string> get_template_for_cluster_id(int cluster_id) const;

    /**
     * Get the cluster ID for a log line
     */
    int get_cluster_id_for_log(std::string_view line) const;

    /**
     * Get all templates with their cluster IDs
     */
    folly::F14FastMap<int, std::string> get_all_templates() const;

    /**
     * Get the cluster ID from a LogRecordObject
     */
    std::optional<int> get_cluster_id_from_record(const LogRecordObject& record) const;

private:
    // Implementation (PIMPL)
    std::unique_ptr<DrainParserImpl> impl_;

    // Store the user’s DataLoaderConfig reference (if you need it in the future)
    const DataLoaderConfig& user_config_;
};

} // namespace logai

