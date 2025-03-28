// ============================================================================
// drain_parser.h
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <folly/FBString.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include "log_parser.h"
#include "data_loader_config.h"

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
    folly::F14FastSet<std::string> pool_;
};

/**
 * DRAIN log parser - A high-performance implementation of the DRAIN log parsing algorithm.
 */
class DrainParser : public LogParser {
public:
    /**
     * Constructor for DrainParser
     * @param config Configuration for the parser
     */
    explicit DrainParser(const DataLoaderConfig& config);
    ~DrainParser() override;

    // Implement pure virtual methods from LogParser
    LogParser::LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;

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

    /**
     * Get template attributes for a cluster ID
     */
    std::vector<std::pair<std::string, std::string>> get_template_attributes(int cluster_id) const;

    /**
     * Get all templates (alias for get_all_templates for backward compatibility)
     */
    folly::F14FastMap<int, std::string> get_templates() const {
        return get_all_templates();
    }

private:
    // Implementation (PIMPL)
    std::unique_ptr<DrainParserImpl> impl_;

    // Store the user's DataLoaderConfig reference
    DataLoaderConfig user_config_;
};

} // namespace logai

