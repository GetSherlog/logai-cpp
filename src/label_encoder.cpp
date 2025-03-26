/**
 * @file label_encoder.cpp
 * @brief Implementation of the label encoder for categorical data
 */

#include "label_encoder.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <sstream>

namespace logai {

LabelEncoder::LabelEncoder() : is_fitted_(false) {}

bool LabelEncoder::fit_transform(duckdb::Connection& conn, 
                               const std::string& input_table, 
                               const std::string& output_table) {
    // Reset the encoder state
    column_mappings_.clear();
    is_fitted_ = false;
    
    try {
        // Check if input table exists
        auto check_result = conn.Query("SELECT 1 FROM " + input_table + " LIMIT 1");
        if (check_result->HasError()) {
            throw std::runtime_error("Input table not found: " + input_table);
        }
        
        // Get column names and types from the input table
        auto columns_result = conn.Query("PRAGMA table_info('" + input_table + "')");
        if (columns_result->HasError()) {
            throw std::runtime_error("Failed to get table info for: " + input_table);
        }
        
        // Create output table with the same structure as input table
        std::string create_sql = "CREATE TABLE " + output_table + " AS SELECT * FROM " + input_table + " WHERE 1=0";
        auto create_result = conn.Query(create_sql);
        if (create_result->HasError()) {
            throw std::runtime_error("Failed to create output table: " + create_result->GetError());
        }
        
        // Process each column in the table
        for (size_t i = 0; i < columns_result->RowCount(); i++) {
            std::string column_name = columns_result->GetValue(1, i).ToString();
            std::string column_type = columns_result->GetValue(2, i).ToString();
            
            // Only process string/varchar columns
            if (column_type.find("VARCHAR") != std::string::npos || 
                column_type.find("TEXT") != std::string::npos || 
                column_type.find("CHAR") != std::string::npos) {
                
                // Add encoded column to output table
                std::string encoded_name = column_name + "_categorical";
                std::string alter_sql = "ALTER TABLE " + output_table + " ADD COLUMN " + encoded_name + " INTEGER";
                
                auto alter_result = conn.Query(alter_sql);
                if (alter_result->HasError()) {
                    throw std::runtime_error("Failed to add column to output table: " + alter_result->GetError());
                }
                
                // Encode the column (and fit the encoder)
                if (!encode_column(conn, input_table, output_table, column_name, true)) {
                    return false;
                }
            }
        }
        
        // Copy the original data to the output table
        std::string copy_sql = "INSERT INTO " + output_table + " SELECT * FROM " + input_table;
        auto copy_result = conn.Query(copy_sql);
        if (copy_result->HasError()) {
            throw std::runtime_error("Failed to copy data to output table: " + copy_result->GetError());
        }
        
        // Mark encoder as fitted
        is_fitted_ = true;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in fit_transform: " << e.what() << std::endl;
        return false;
    }
}

bool LabelEncoder::transform(duckdb::Connection& conn, 
                           const std::string& input_table, 
                           const std::string& output_table) const {
    if (!is_fitted_) {
        throw std::runtime_error("Encoder must be fitted before transform can be called");
    }
    
    try {
        // Check if input table exists
        auto check_result = conn.Query("SELECT 1 FROM " + input_table + " LIMIT 1");
        if (check_result->HasError()) {
            throw std::runtime_error("Input table not found: " + input_table);
        }
        
        // Get column names and types from the input table
        auto columns_result = conn.Query("PRAGMA table_info('" + input_table + "')");
        if (columns_result->HasError()) {
            throw std::runtime_error("Failed to get table info for: " + input_table);
        }
        
        // Create output table with the same structure as input table
        std::string create_sql = "CREATE TABLE " + output_table + " AS SELECT * FROM " + input_table + " WHERE 1=0";
        auto create_result = conn.Query(create_sql);
        if (create_result->HasError()) {
            throw std::runtime_error("Failed to create output table: " + create_result->GetError());
        }
        
        // Process each column in the table
        for (size_t i = 0; i < columns_result->RowCount(); i++) {
            std::string column_name = columns_result->GetValue(1, i).ToString();
            std::string column_type = columns_result->GetValue(2, i).ToString();
            
            // Only process string/varchar columns that have been fitted
            if ((column_type.find("VARCHAR") != std::string::npos || 
                 column_type.find("TEXT") != std::string::npos || 
                 column_type.find("CHAR") != std::string::npos) && 
                column_mappings_.find(column_name) != column_mappings_.end()) {
                
                // Add encoded column to output table
                std::string encoded_name = column_name + "_categorical";
                std::string alter_sql = "ALTER TABLE " + output_table + " ADD COLUMN " + encoded_name + " INTEGER";
                
                auto alter_result = conn.Query(alter_sql);
                if (alter_result->HasError()) {
                    throw std::runtime_error("Failed to add column to output table: " + alter_result->GetError());
                }
                
                // Encode the column (without fitting)
                if (!encode_column(conn, input_table, output_table, column_name, false)) {
                    return false;
                }
            }
        }
        
        // Copy the original data to the output table
        std::string copy_sql = "INSERT INTO " + output_table + " SELECT * FROM " + input_table;
        auto copy_result = conn.Query(copy_sql);
        if (copy_result->HasError()) {
            throw std::runtime_error("Failed to copy data to output table: " + copy_result->GetError());
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in transform: " << e.what() << std::endl;
        return false;
    }
}

bool LabelEncoder::is_fitted() const {
    return is_fitted_;
}

std::vector<std::string> LabelEncoder::get_classes(const std::string& column_name) const {
    if (!is_fitted_) {
        throw std::runtime_error("Encoder must be fitted before get_classes can be called");
    }
    
    auto it = column_mappings_.find(column_name);
    if (it == column_mappings_.end()) {
        throw std::runtime_error("Column '" + column_name + "' not found in encoder");
    }
    
    // Create a vector with size equal to the number of classes
    std::vector<std::string> classes(it->second.size());
    
    // Populate the vector based on the index mapping
    for (const auto& [value, index] : it->second) {
        classes[index] = value;
    }
    
    return classes;
}

bool LabelEncoder::encode_column(duckdb::Connection& conn,
                               const std::string& input_table,
                               const std::string& output_table,
                               const std::string& column_name,
                               bool fit) const {
    try {
        // Get or create mapping for this column
        if (fit) {
            // If fitting, create a new mapping
            auto& mutable_self = const_cast<LabelEncoder&>(*this);
            
            // Query to get distinct values
            std::string distinct_sql = "SELECT DISTINCT " + column_name + " FROM " + input_table + 
                                      " WHERE " + column_name + " IS NOT NULL ORDER BY " + column_name;
                                      
            auto distinct_result = conn.Query(distinct_sql);
            if (distinct_result->HasError()) {
                throw std::runtime_error("Failed to get distinct values: " + distinct_result->GetError());
            }
            
            // Assign indices to unique values
            for (size_t i = 0; i < distinct_result->RowCount(); i++) {
                std::string value = distinct_result->GetValue(0, i).ToString();
                mutable_self.column_mappings_[column_name][value] = static_cast<int>(i);
            }
        }
        
        // Create a CASE statement to map values to indices
        std::stringstream case_sql;
        case_sql << "UPDATE " + output_table + " SET " + column_name + "_categorical = CASE ";
        
        auto mapping_it = column_mappings_.find(column_name);
        if (mapping_it != column_mappings_.end()) {
            for (const auto& [value, index] : mapping_it->second) {
                case_sql << "WHEN " + column_name + " = '" << value << "' THEN " << index << " ";
            }
        }
        
        case_sql << "ELSE NULL END";
        
        // Execute the update
        auto update_result = conn.Query(case_sql.str());
        if (update_result->HasError()) {
            throw std::runtime_error("Failed to update encoded values: " + update_result->GetError());
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error encoding column " << column_name << ": " << e.what() << std::endl;
        return false;
    }
}

} // namespace logai 