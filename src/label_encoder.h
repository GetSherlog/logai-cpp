/**
 * @file label_encoder.h
 * @brief Label encoder for categorical data
 * 
 * This file defines a label encoder for categorical string data in logai.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <duckdb.hpp>

namespace logai {

/**
 * @brief Label encoder for categorical data
 * 
 * This class provides functionality to encode categorical string data into
 * numerical labels. It maintains a mapping between string values and their
 * corresponding numerical encodings.
 */
class LabelEncoder {
public:
    /**
     * @brief Construct a new LabelEncoder object
     */
    LabelEncoder();

    /**
     * @brief Fit the encoder on the given categorical data and transform it
     * 
     * @param conn DuckDB connection to use
     * @param input_table The name of the input table containing categorical columns
     * @param output_table The name of the output table to create with encoded columns
     * @return bool true if successful, false otherwise
     */
    bool fit_transform(duckdb::Connection& conn, 
                      const std::string& input_table, 
                      const std::string& output_table);
    
    /**
     * @brief Transform categorical data using the fitted encoder
     * 
     * @param conn DuckDB connection to use
     * @param input_table The name of the input table containing categorical columns
     * @param output_table The name of the output table to create with encoded columns
     * @return bool true if successful, false otherwise
     */
    bool transform(duckdb::Connection& conn, 
                  const std::string& input_table, 
                  const std::string& output_table) const;
    
    /**
     * @brief Check if the encoder has been fitted
     * 
     * @return true If the encoder has been fitted
     * @return false If the encoder needs to be fitted
     */
    bool is_fitted() const;
    
    /**
     * @brief Get the classes for a column
     * 
     * @param column_name The name of the column
     * @return std::vector<std::string> The classes for the column
     */
    std::vector<std::string> get_classes(const std::string& column_name) const;

private:
    // Maps column names to their string->index mappings
    std::unordered_map<std::string, std::unordered_map<std::string, int>> column_mappings_;
    
    // Flag to indicate if the encoder has been fitted
    bool is_fitted_ = false;
    
    // Private helper method to encode a single column
    bool encode_column(duckdb::Connection& conn,
                      const std::string& input_table,
                      const std::string& output_table,
                      const std::string& column_name,
                      bool fit) const;
};

} // namespace logai 