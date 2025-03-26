#include "feature_extractor.h"
#include <algorithm>
#include <execution>
#include <numeric>
#include <unordered_map>
#include <chrono>
#include <string>
#include <sstream>
#include <iostream>
#include <absl/strings/str_join.h>

namespace logai {

namespace {
// Helper function to parse time frequency string (e.g., "1s", "1m", "1h")
std::chrono::seconds parse_time_frequency(const std::string& freq) {
    if (freq.empty()) {
        return std::chrono::seconds(0);
    }
    
    size_t value_end = 0;
    int value = std::stoi(freq, &value_end);
    
    char unit = freq[value_end];
    switch (unit) {
        case 's': return std::chrono::seconds(value);
        case 'm': return std::chrono::minutes(value);
        case 'h': return std::chrono::hours(value);
        case 'd': return std::chrono::hours(value * 24);
        default:  return std::chrono::seconds(value);
    }
}

// Helper function to floor a timestamp to the given frequency
std::chrono::system_clock::time_point floor_time(
    const std::chrono::system_clock::time_point& tp, 
    const std::chrono::seconds& freq) {
    
    if (freq.count() == 0) {
        return tp;
    }
    
    auto duration = tp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    
    // Calculate floored seconds
    seconds = std::chrono::seconds(seconds.count() / freq.count() * freq.count());
    
    return std::chrono::system_clock::time_point(seconds);
}

// Helper to create a group key from attributes and timestamp
std::unordered_map<std::string, std::string> create_group_key(
    const LogRecordObject& log,
    const std::vector<std::string>& group_by_category,
    const std::chrono::system_clock::time_point& time_bucket) {
    
    std::unordered_map<std::string, std::string> key;
    
    // Add category attributes to key
    for (const auto& category : group_by_category) {
        auto it = log.attributes.find(category);
        if (it != log.attributes.end()) {
            key[category] = it->second;
        } else {
            key[category] = "";  // Use empty string for missing values
        }
    }
    
    // Add time bucket to key if applicable
    if (time_bucket != std::chrono::system_clock::time_point{}) {
        // Format time bucket as ISO 8601 string
        auto time_t = std::chrono::system_clock::to_time_t(time_bucket);
        std::tm tm = *std::localtime(&time_t);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
        key["timestamp"] = buffer;
    }
    
    return key;
}

} // anonymous namespace

FeatureExtractor::FeatureExtractor(const FeatureExtractorConfig& config)
    : config_(config) {
}

std::chrono::system_clock::time_point FeatureExtractor::get_time_bucket(
    const std::chrono::system_clock::time_point& timestamp) {
    
    if (config_.group_by_time.empty()) {
        return std::chrono::system_clock::time_point{};
    }
    
    auto freq = parse_time_frequency(config_.group_by_time);
    return floor_time(timestamp, freq);
}

std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>>
FeatureExtractor::group_logs(const std::vector<LogRecordObject>& logs) {
    // Map to accumulate indices by group key
    std::unordered_map<std::string, std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>> groups;
    
    // Parse time frequency once
    auto time_freq = parse_time_frequency(config_.group_by_time);
    
    // Process each log record
    for (size_t i = 0; i < logs.size(); ++i) {
        const auto& log = logs[i];
        
        // Get time bucket if needed
        std::chrono::system_clock::time_point time_bucket;
        if (!config_.group_by_time.empty() && log.timestamp) {
            time_bucket = floor_time(*log.timestamp, time_freq);
        }
        
        // Create group key
        auto key_map = create_group_key(log, config_.group_by_category, time_bucket);
        
        // Create string representation of key for map lookup
        std::string key_str;
        for (const auto& [k, v] : key_map) {
            key_str += k + ":" + v + ";";
        }
        
        // Add to group
        auto& group = groups[key_str];
        group.first = std::move(key_map);
        group.second.push_back(i);
    }
    
    // Convert map to vector of pairs
    std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>> result;
    result.reserve(groups.size());
    
    for (auto& [_, group] : groups) {
        result.emplace_back(std::move(group.first), std::move(group.second));
    }
    
    return result;
}

std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>>
FeatureExtractor::apply_sliding_window(
    const std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>>& grouped_logs) {
    
    if (config_.sliding_window <= 0) {
        return grouped_logs;
    }
    
    if (config_.steps <= 0) {
        throw std::runtime_error("Steps should be greater than zero. Steps: " + 
                                std::to_string(config_.steps));
    }
    
    std::vector<std::pair<std::unordered_map<std::string, std::string>, std::vector<size_t>>> result;
    
    for (const auto& [group_key, indices] : grouped_logs) {
        if (indices.size() <= static_cast<size_t>(config_.sliding_window)) {
            // If group size <= window size, keep as is
            result.emplace_back(group_key, indices);
        } else {
            // Apply sliding window
            for (size_t i = 0; i + config_.sliding_window <= indices.size(); i += config_.steps) {
                std::vector<size_t> window_indices(indices.begin() + i, 
                                                 indices.begin() + i + config_.sliding_window);
                result.emplace_back(group_key, std::move(window_indices));
            }
        }
    }
    
    return result;
}

FeatureExtractionResult FeatureExtractor::convert_to_counter_vector(
    const std::vector<LogRecordObject>& logs) {
    
    FeatureExtractionResult result;
    
    // Group logs based on configuration
    auto grouped_logs = group_logs(logs);
    
    // Apply sliding window if configured
    if (config_.sliding_window > 0) {
        grouped_logs = apply_sliding_window(grouped_logs);
    }
    
    // Prepare result structure
    result.event_indices.reserve(grouped_logs.size());
    result.group_identifiers.reserve(grouped_logs.size());
    result.counts.reserve(grouped_logs.size());
    
    // Fill result with grouped data
    for (const auto& [group_key, indices] : grouped_logs) {
        result.event_indices.push_back(indices);
        result.group_identifiers.push_back(group_key);
        result.counts.push_back(indices.size());
    }
    
    return result;
}

FeatureExtractionResult FeatureExtractor::convert_to_feature_vector(
    const std::vector<LogRecordObject>& logs,
    duckdb::Connection& conn,
    const std::string& log_vectors_table,
    const std::string& output_table) {
    
    FeatureExtractionResult result;
    
    // Group logs based on configuration
    auto grouped_logs = group_logs(logs);
    
    // Apply sliding window if configured
    if (config_.sliding_window > 0) {
        grouped_logs = apply_sliding_window(grouped_logs);
    }
    
    // Prepare result structure
    result.event_indices.reserve(grouped_logs.size());
    result.group_identifiers.reserve(grouped_logs.size());
    
    try {
        // Check if log vectors table exists
        auto check_result = conn.Query("SELECT COUNT(*) FROM " + log_vectors_table + " LIMIT 1");
        if (check_result->HasError()) {
            throw std::runtime_error("Log vectors table not found: " + log_vectors_table);
        }
        
        // Get column names from the log vectors table
        auto columns_result = conn.Query("PRAGMA table_info('" + log_vectors_table + "')");
        if (columns_result->HasError()) {
            throw std::runtime_error("Failed to get columns for table: " + log_vectors_table);
        }
        
        std::vector<std::string> feature_columns;
        for (size_t i = 0; i < columns_result->RowCount(); i++) {
            // Column name is at index 1
            std::string col_name = columns_result->GetValue(1, i).ToString();
            // Skip the id/index column
            if (col_name != "id" && col_name != "index") {
                feature_columns.push_back(col_name);
            }
        }
        
        if (feature_columns.empty()) {
            throw std::runtime_error("No feature columns found in table: " + log_vectors_table);
        }
        
        // Create the output feature vectors table
        std::string create_table_sql = "CREATE TABLE " + output_table + " (";
        create_table_sql += "group_id INTEGER, ";
        
        // Add group identifier columns
        if (!grouped_logs.empty() && !grouped_logs[0].first.empty()) {
            for (const auto& [key, _] : grouped_logs[0].first) {
                create_table_sql += "\"" + key + "\" VARCHAR, ";
            }
        }
        
        // Add feature columns
        for (size_t i = 0; i < feature_columns.size(); i++) {
            create_table_sql += "\"feature_" + std::to_string(i) + "\" DOUBLE";
            if (i < feature_columns.size() - 1) {
                create_table_sql += ", ";
            }
        }
        create_table_sql += ")";
        
        // Execute create table SQL
        auto create_result = conn.Query(create_table_sql);
        if (create_result->HasError()) {
            throw std::runtime_error("Failed to create output table: " + create_result->GetError());
        }
        
        // Process each group
        for (size_t group_id = 0; group_id < grouped_logs.size(); group_id++) {
            const auto& [group_key, indices] = grouped_logs[group_id];
            
            // Store group metadata
            result.event_indices.push_back(indices);
            result.group_identifiers.push_back(group_key);
            
            // Create comma-separated list of indices for IN clause
            std::string indices_str;
            for (size_t i = 0; i < indices.size(); i++) {
                indices_str += std::to_string(indices[i]);
                if (i < indices.size() - 1) {
                    indices_str += ", ";
                }
            }
            
            // Calculate mean of feature vectors for this group
            std::string feature_calc_sql = "INSERT INTO " + output_table + " SELECT ";
            feature_calc_sql += std::to_string(group_id) + " AS group_id, ";
            
            // Add group identifiers
            for (const auto& [key, value] : group_key) {
                feature_calc_sql += "'" + value + "' AS \"" + key + "\", ";
            }
            
            // Add feature averages
            for (size_t i = 0; i < feature_columns.size(); i++) {
                feature_calc_sql += "AVG(" + feature_columns[i] + ") AS \"feature_" + std::to_string(i) + "\"";
                if (i < feature_columns.size() - 1) {
                    feature_calc_sql += ", ";
                }
            }
            
            feature_calc_sql += " FROM " + log_vectors_table;
            
            if (!indices.empty()) {
                feature_calc_sql += " WHERE id IN (" + indices_str + ")";
            }
            
            // Execute feature calculation SQL
            auto calc_result = conn.Query(feature_calc_sql);
            if (calc_result->HasError()) {
                std::cerr << "Error calculating features for group " << group_id << ": " 
                          << calc_result->GetError() << std::endl;
            }
        }
        
        // Set the output table name in the result
        result.feature_vectors_table = output_table;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in convert_to_feature_vector: " << e.what() << std::endl;
    }
    
    return result;
}

FeatureExtractionResult FeatureExtractor::convert_to_sequence(
    const std::vector<LogRecordObject>& logs) {
    
    FeatureExtractionResult result;
    
    // Group logs based on configuration
    auto grouped_logs = group_logs(logs);
    
    // Apply sliding window if configured
    if (config_.sliding_window > 0) {
        grouped_logs = apply_sliding_window(grouped_logs);
    }
    
    // Prepare result structure
    result.event_indices.reserve(grouped_logs.size());
    result.group_identifiers.reserve(grouped_logs.size());
    result.sequences.reserve(grouped_logs.size());
    
    // Process each group
    for (const auto& [group_key, indices] : grouped_logs) {
        // Store group metadata
        result.event_indices.push_back(indices);
        result.group_identifiers.push_back(group_key);
        
        // Create sequence by joining log bodies
        std::vector<std::string> log_bodies;
        log_bodies.reserve(indices.size());
        
        for (size_t idx : indices) {
            if (idx < logs.size()) {
                log_bodies.push_back(logs[idx].body);
            }
        }
        
        // Join log bodies with spaces
        result.sequences.push_back(absl::StrJoin(log_bodies, " "));
    }
    
    return result;
}

} // namespace logai 