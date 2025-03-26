#include "duckdb_store.h"
#include <spdlog/spdlog.h>
#include <iostream>

namespace logai {

DuckDBStore::DuckDBStore() {
    try {
        // Create an in-memory DuckDB database
        m_db_path = ":memory:";  // Use in-memory database by default
        m_db = std::make_unique<duckdb::DuckDB>(m_db_path);
        m_conn = std::make_unique<duckdb::Connection>(*m_db);
        
        spdlog::info("DuckDB store initialized with in-memory database");
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize DuckDB store: {}", e.what());
        throw;
    }
}

DuckDBStore::~DuckDBStore() = default;

bool DuckDBStore::init_template_table(const std::string& template_id,
                                    const std::vector<std::string>& columns,
                                    const std::vector<std::string>& types) {
    if (columns.size() != types.size()) {
        spdlog::error("Columns and types must have the same size");
        return false;
    }

    try {
        // Create table name
        std::string table_name = "template_" + template_id;
        
        // Build CREATE TABLE statement
        std::string create_stmt = "CREATE TABLE IF NOT EXISTS " + table_name + " (";
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) create_stmt += ", ";
            create_stmt += columns[i] + " " + types[i];
        }
        create_stmt += ")";

        // Execute CREATE TABLE with thread-safe connection
        {
            auto conn = conn_.rlock();
            conn->get()->Query(create_stmt);
        }
        
        // Store table name mapping with thread-safe map
        {
            auto tables = template_tables_.wlock();
            (*tables)[template_id] = table_name;
        }
        
        spdlog::info("Created table {} for template {}", table_name, template_id);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create table for template {}: {}", template_id, e.what());
        return false;
    }
}

std::string DuckDBStore::build_insert_stmt(const std::string& table_name, const LogRecordObject& record) {
    std::string insert_stmt = "INSERT INTO " + table_name + " VALUES (";
    insert_stmt += "'" + record.timestamp + "', ";
    insert_stmt += "'" + record.level + "', ";
    insert_stmt += "'" + record.body + "'";
    insert_stmt += ")";
    return insert_stmt;
}

bool DuckDBStore::insert_log_record(const std::string& template_id,
                                  const LogRecordObject& record) {
    // Get table name with thread-safe access
    std::string table_name;
    {
        auto tables = template_tables_.rlock();
        auto it = tables->find(template_id);
        if (it == tables->end()) {
            spdlog::error("No table found for template {}", template_id);
            return false;
        }
        table_name = it->second;
    }

    try {
        // Build and execute INSERT statement with thread-safe connection
        std::string insert_stmt = build_insert_stmt(table_name, record);
        {
            auto conn = conn_.rlock();
            conn->get()->Query(insert_stmt);
        }
        
        // Clear query cache for this table
        {
            auto cache = query_cache_.wlock();
            cache->erase(table_name);
        }
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to insert record for template {}: {}", template_id, e.what());
        return false;
    }
}

std::vector<std::vector<std::string>> DuckDBStore::execute_query(const std::string& query) {
    // Check cache first
    {
        auto cache = query_cache_.rlock();
        auto it = cache->find(query);
        if (it != cache->end()) {
            return it->second;
        }
    }

    try {
        std::vector<std::vector<std::string>> rows;
        {
            auto conn = conn_.rlock();
            auto result = conn->get()->Query(query);
            
            while (result->Next()) {
                std::vector<std::string> row;
                for (size_t i = 0; i < result->ColumnCount(); ++i) {
                    row.push_back(result->GetValue(i).ToString());
                }
                rows.push_back(std::move(row));
            }
        }
        
        // Cache the results
        {
            auto cache = query_cache_.wlock();
            (*cache)[query] = rows;
        }
        
        return rows;
    } catch (const std::exception& e) {
        spdlog::error("Failed to execute query: {}", e.what());
        return {};
    }
}

std::vector<std::pair<std::string, std::string>> DuckDBStore::get_schema(const std::string& template_id) {
    std::string table_name;
    {
        auto tables = template_tables_.rlock();
        auto it = tables->find(template_id);
        if (it == tables->end()) {
            return {};
        }
        table_name = it->second;
    }

    try {
        std::vector<std::pair<std::string, std::string>> schema;
        {
            auto conn = conn_.rlock();
            auto result = conn->get()->Query("DESCRIBE " + table_name);
            
            while (result->Next()) {
                std::string column = result->GetValue(0).ToString();
                std::string type = result->GetValue(1).ToString();
                schema.emplace_back(column, type);
            }
        }
        
        return schema;
    } catch (const std::exception& e) {
        spdlog::error("Failed to get schema for template {}: {}", template_id, e.what());
        return {};
    }
}

std::string DuckDBStore::get_db_path() const {
    return m_db_path;
}

} // namespace logai 