#pragma once

#include "log_parser.h"
#include "data_loader_config.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <regex>
#include <unordered_set>
#include <optional>

namespace logai {

// Forward declaration of the implementation class
class DrainParserImpl;

// String pool for interning common strings to reduce memory usage
class StringPool {
public:
    std::string_view intern(std::string_view str) {
        // Fast path for empty strings
        if (str.empty()) {
            static const std::string empty;
            return empty;
        }
        
        // Check if the string is already in the pool
        auto it = pool_.find(std::string(str));
        if (it != pool_.end()) {
            return *it;
        }
        
        // Insert the string into the pool
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
 * DRAIN log parser - A high-performance implementation of the DRAIN log parsing algorithm
 * 
 * DRAIN is a log parsing algorithm that uses a fixed-depth parse tree to efficiently
 * group similar log messages and extract their templates and parameters.
 * 
 * This implementation is optimized for high-performance C++ processing.
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
    ~DrainParser();
    
    /**
     * Parse a log line using the DRAIN algorithm
     * 
     * @param line The log line to parse
     * @return A LogRecordObject containing the parsed log with template information
     */
    LogRecordObject parse_line(std::string_view line) override;

    /**
     * Set the maximum depth of the parse tree
     * 
     * @param depth The maximum depth (default: 4)
     */
    void setDepth(int depth);

    /**
     * Set the similarity threshold for grouping logs
     * 
     * @param threshold The similarity threshold (default: 0.5)
     */
    void setSimilarityThreshold(const double threshold);
    
    /**
     * Set custom regex patterns for log preprocessing
     * 
     * @param pattern_strings Vector of regex pattern strings to use for preprocessing
     */
    void set_preprocess_patterns(const std::vector<std::string>& pattern_strings);

    std::optional<std::string> get_template_for_cluster_id(const int cluster_id) const;
    
    /**
     * Get the cluster ID for a log line
     * 
     * @param line The log line
     * @return The cluster/template ID for the log line
     */
    int get_cluster_id_for_log(std::string_view line) const;

    /**
     * Get all templates with their cluster IDs
     * 
     * @return A map of cluster IDs to their templates
     */
    folly::F14FastMap<int, std::string> get_all_templates() const;
    
    /**
     * Get the cluster ID from a LogRecordObject
     * 
     * @param record The LogRecordObject to get the cluster ID from
     * @return An optional containing the cluster ID if it exists
     */
    std::optional<int> get_cluster_id_from_record(const LogRecordObject& record) const;

private:
    // Use PIMPL idiom to hide implementation details
    std::unique_ptr<DrainParserImpl> impl_;
    const DataLoaderConfig& config_;
};

} // namespace logai 