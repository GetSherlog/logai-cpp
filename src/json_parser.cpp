#include "json_parser.h"
#include <nlohmann/json.hpp>

namespace logai {

JsonParser::JsonParser(const DataLoaderConfig& config) : config_(config) {}

std::optional<std::chrono::system_clock::time_point> JsonParser::parse_timestamp(std::string_view timestamp, const std::string& format) {
    // Simple implementation - in a real application, this would parse the timestamp string
    // according to the provided format
    return std::nullopt;
}

LogEntry JsonParser::parse(const std::string& line) {
    LogEntry entry;
    try {
        // Use the existing parse_line implementation to get the LogRecordObject
        LogRecordObject record = parse_line(line);
        
        // Convert LogRecordObject to LogEntry
        entry.timestamp = record.timestamp ? std::to_string(record.timestamp->time_since_epoch().count()) : "";
        entry.level = record.severity;
        entry.message = record.body;
        
        // Copy attributes to fields
        for (const auto& [key, value] : record.attributes) {
            entry.fields[std::string(key)] = std::string(value);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse log line: " + std::string(e.what()));
    }
    
    return entry;
}

bool JsonParser::validate(const std::string& line) {
    try {
        nlohmann::json::parse(line);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

LogRecordObject JsonParser::parse_line(const std::string& line) {
    LogRecordObject record;
    try {
        nlohmann::json json = nlohmann::json::parse(line);
        
        // Extract fields based on config dimensions
        for (const auto& dimension : config_.dimensions) {
            if (json.contains(dimension)) {
                if (dimension == "body") {
                    record.body = json[dimension].get<std::string>();
                } else if (dimension == "timestamp") {
                    record.timestamp = parse_timestamp(json[dimension].get<std::string>(), config_.datetime_format);
                } else if (dimension == "severity") {
                    record.severity = json[dimension].get<std::string>();
                } else {
                    // Store all other fields in attributes
                    record.attributes[dimension] = json[dimension].get<std::string>();
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Failed to parse JSON line: " + std::string(e.what()));
    }
    
    return record;
}
} 