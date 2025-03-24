#pragma once
#include <string>
#include <memory>
#include <vector>
#include <folly/container/F14Map.h>
#include <folly/Synchronized.h>
#include <duckdb.hpp>
#include "log_record.h"

namespace logai {

class DuckDBStore {
public:
    DuckDBStore();
    ~DuckDBStore();

    // Initialize the database with a schema for a template
    bool init_template_table(const std::string& template_id, 
                           const std::vector<std::string>& columns,
                           const std::vector<std::string>& types);

    // Insert a log record into the appropriate template table
    bool insert_log_record(const std::string& template_id, 
                         const LogRecordObject& record);

    // Execute a query on a template table
    std::vector<std::vector<std::string>> execute_query(const std::string& query);

    // Get schema for a template table
    std::vector<std::pair<std::string, std::string>> get_schema(const std::string& template_id);

private:
    // Thread-safe database connection
    folly::Synchronized<std::unique_ptr<duckdb::DuckDB>> db_;
    folly::Synchronized<std::unique_ptr<duckdb::Connection>> conn_;
    
    // Thread-safe template table mapping
    folly::Synchronized<folly::F14FastMap<std::string, std::string>> template_tables_;
    
    // Thread-safe query cache
    folly::Synchronized<folly::F14FastMap<std::string, std::vector<std::vector<std::string>>>> query_cache_;
    
    // Helper methods
    std::string build_insert_stmt(const std::string& table_name, const LogRecordObject& record);
};

} // namespace logai 