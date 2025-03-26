#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <tuple>
#include <memory>
#include <optional>
#include <duckdb.hpp>
#include "log_record.h"
#include "simd_string_ops.h"

namespace logai {

/**
 * @brief Configuration for the Preprocessor
 * 
 * This class stores configuration parameters for the log preprocessor.
 */
class PreprocessorConfig {
public:
    /**
     * @brief Constructor with parameters
     * 
     * @param custom_delimiters_regex A map of delimiter regex patterns to replace in raw log data
     * @param custom_replace_list A list of tuples of regex patterns and their replacements
     * @param use_simd Whether to use SIMD optimizations where possible
     */
    PreprocessorConfig(
        std::unordered_map<std::string, std::string> custom_delimiters_regex = {},
        std::vector<std::tuple<std::string, std::string>> custom_replace_list = {},
        bool use_simd = true
    );

    std::unordered_map<std::string, std::string> custom_delimiters_regex;
    std::vector<std::tuple<std::string, std::string>> custom_replace_list;
    bool use_simd;
};

/**
 * @brief Preprocessor class that contains common preprocessing methods for log data
 * 
 * This class provides functionality to clean and preprocess log data before parsing.
 */
class Preprocessor {
public:
    /**
     * @brief Constructor
     * 
     * @param config The preprocessor configuration
     */
    explicit Preprocessor(const PreprocessorConfig& config);

    /**
     * @brief Clean a single log line
     * 
     * @param logline The raw log line to clean
     * @return A tuple containing the cleaned log line and any extracted terms
     */
    std::tuple<std::string, std::unordered_map<std::string, std::vector<std::string>>> 
    clean_log_line(std::string_view logline);

    /**
     * @brief Clean multiple log lines in a batch (optimized for memory efficiency)
     * 
     * @param loglines Vector of log lines to clean
     * @return Vector of cleaned log lines and a map of extracted terms
     */
    std::tuple<std::vector<std::string>, std::unordered_map<std::string, std::vector<std::vector<std::string>>>>
    clean_log_batch(const std::vector<std::string>& loglines);

    /**
     * @brief Identify timestamps in a log record
     * 
     * @param logrecord The log record to analyze
     * @return Optional timestamp if identified
     */
    std::optional<std::chrono::system_clock::time_point> identify_timestamps(const LogRecordObject& logrecord);

    /**
     * @brief Group log entries by specified attributes
     * 
     * @param conn DuckDB connection to use
     * @param table_name Name of the table containing log attributes
     * @param by Vector of column names to group by
     * @param result_table Name of the output table to create with grouped indices
     * @return True if grouping was successful, false otherwise
     */
    bool group_log_index(
        duckdb::Connection& conn, 
        const std::string& table_name,
        const std::vector<std::string>& by,
        const std::string& result_table
    );

private:
    PreprocessorConfig config_;
    std::vector<std::regex> delimiter_regexes_;
    std::vector<std::pair<std::regex, std::string>> replacement_regexes_;
    
    // SIMD optimized versions
    std::tuple<std::string, std::unordered_map<std::string, std::vector<std::string>>> 
    clean_log_line_simd(std::string_view logline);
    
    std::tuple<std::vector<std::string>, std::unordered_map<std::string, std::vector<std::vector<std::string>>>>
    clean_log_batch_simd(const std::vector<std::string>& loglines);
    
    // Prepare character sets for SIMD-based delimiter replacements
    std::vector<char> prepare_delimiter_char_set() const;
    
    // Initialize regex patterns from config
    void initialize_patterns();
};

} // namespace logai 