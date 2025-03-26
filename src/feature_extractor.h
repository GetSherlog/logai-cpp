#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <functional>
#include <duckdb.hpp>
#include "log_record.h"

namespace logai {

/**
 * Configuration class for Feature Extractor.
 * 
 * This class holds parameters for how log data should be grouped and processed.
 */
struct FeatureExtractorConfig {
    // Optional categories to group logs by
    std::vector<std::string> group_by_category;
    
    // Grouping log lines by time frequency (e.g., "1s", "1m", "1h")
    std::string group_by_time;
    
    // The length of the sliding window
    int sliding_window = 0;
    
    // The step-size of sliding window
    int steps = 1;
    
    // The pad the log vector to this size
    int max_feature_len = 100;
};

/**
 * Result of feature extraction containing grouped log data
 */
struct FeatureExtractionResult {
    // Event indices after grouping
    std::vector<std::vector<size_t>> event_indices;
    
    // Group identifiers based on the grouping criteria
    std::vector<std::unordered_map<std::string, std::string>> group_identifiers;
    
    // Counts for each group
    std::vector<size_t> counts;
    
    // Feature vectors for each group (if applicable)
    std::string feature_vectors_table;
    
    // Sequence data for each group (if applicable)
    std::vector<std::string> sequences;
};

/**
 * Feature Extractor for log data
 * 
 * This class combines structured log attributes and log vectors. It can group
 * log records based on user-defined strategies such as by categorical attributes
 * or by timestamps.
 */
class FeatureExtractor {
public:
    /**
     * Constructor
     * 
     * @param config The configuration for the feature extractor
     */
    explicit FeatureExtractor(const FeatureExtractorConfig& config);
    
    /**
     * Converts logs to counter vector after grouping
     * 
     * @param logs Vector of log records to process
     * @return FeatureExtractionResult containing grouped counter vectors
     */
    FeatureExtractionResult convert_to_counter_vector(
        const std::vector<LogRecordObject>& logs);
    
    /**
     * Converts logs to feature vector after grouping
     * 
     * @param logs Vector of log records to process
     * @param conn DuckDB connection to use
     * @param log_vectors_table Name of table containing log vectors
     * @param output_table Name of the output table for feature vectors
     * @return FeatureExtractionResult containing grouped feature vectors
     */
    FeatureExtractionResult convert_to_feature_vector(
        const std::vector<LogRecordObject>& logs,
        duckdb::Connection& conn,
        const std::string& log_vectors_table,
        const std::string& output_table);
    
    /**
     * Converts logs to sequence using sliding window
     * 
     * @param logs Vector of log records to process
     * @return FeatureExtractionResult containing log sequences
     */
    FeatureExtractionResult convert_to_sequence(
        const std::vector<LogRecordObject>& logs);

private:
    FeatureExtractorConfig config_;
    
    // Helper function to group log records based on configuration
    std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>>
    group_logs(const std::vector<LogRecordObject>& logs);
    
    // Apply sliding window to grouped logs
    std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>>
    apply_sliding_window(
        const std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>>& grouped_logs);
    
    // Extract time bucket from timestamp based on group_by_time config
    std::chrono::system_clock::time_point get_time_bucket(
        const std::chrono::system_clock::time_point& timestamp);
};

} // namespace logai 