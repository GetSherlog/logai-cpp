#include "json_parser.h"
#include <nlohmann/json.hpp>

namespace logai {

JsonParser::JsonParser(const DataLoaderConfig& config) : config_(config) {}

JsonParser::~JsonParser() noexcept {}

std::optional<std::chrono::system_clock::time_point> JsonParser::parse_timestamp(std::string_view timestamp, const std::string& format) {
    // TODO: Move this to a common utility class since it's shared across parsers
    try {
        std::tm tm = {};
        std::stringstream ss(timestamp.data());
        
        // Try parsing with the specified format
        if (!format.empty()) {
            ss >> std::get_time(&tm, format.c_str());
            if (!ss.fail()) {
                return std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        }

        // Fallback formats to try
        const std::vector<const char*> formats = {
            "%Y-%m-%d %H:%M:%S",     // 2023-08-15 14:30:00
            "%Y/%m/%d %H:%M:%S",     // 2023/08/15 14:30:00  
            "%d/%b/%Y:%H:%M:%S",     // 15/Aug/2023:14:30:00
            "%b %d %H:%M:%S",        // Aug 15 14:30:00
            "%Y-%m-%dT%H:%M:%S"      // 2023-08-15T14:30:00 (ISO)
        };

        for (const auto& fmt : formats) {
            ss.clear();
            ss.str(timestamp.data());
            ss >> std::get_time(&tm, fmt);
            if (!ss.fail()) {
                return std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        }

        return std::nullopt;
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

LogParser::LogEntry JsonParser::parse(const std::string& line) {
    LogParser::LogEntry entry;
    try {
        // Use the existing parse_line implementation to get the LogRecordObject
        LogRecordObject record = parse_line(line);
        
        // Convert LogRecordObject to LogEntry
        entry.timestamp = record.timestamp ? std::to_string(record.timestamp->time_since_epoch().count()) : "";
        entry.level = record.level;
        entry.message = record.message;
        
        // Copy fields to fields
        for (const auto& [key, value] : record.fields) {
            entry.fields[key.toStdString()] = value.toStdString();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse log line: " + std::string(e.what()));
    }
    
    return entry;
}

bool JsonParser::validate(const std::string& line) {
    try {
        auto json = nlohmann::json::parse(line);
        return !json.empty();
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
                } else if (dimension == "level") {
                    record.level = json[dimension].get<std::string>();
                } else if (dimension == "message") {
                    record.message = json[dimension].get<std::string>();
                } else {
                    // Store all other fields in fields map
                    record.set_field(dimension, json[dimension].get<std::string>());
                }
            }
        }
        
        // If no dimensions specified, try to extract common fields
        if (config_.dimensions.empty()) {
            if (json.contains("message")) {
                record.message = json["message"].get<std::string>();
            } else if (json.contains("msg")) {
                record.message = json["msg"].get<std::string>();
            }
            
            if (json.contains("level")) {
                record.level = json["level"].get<std::string>();
            } else if (json.contains("severity")) {
                record.level = json["severity"].get<std::string>();
            }
            
            if (json.contains("timestamp")) {
                record.timestamp = parse_timestamp(json["timestamp"].get<std::string>(), config_.datetime_format);
            } else if (json.contains("time")) {
                record.timestamp = parse_timestamp(json["time"].get<std::string>(), config_.datetime_format);
            }
            
            // Add all fields to the fields map
            for (auto it = json.begin(); it != json.end(); ++it) {
                if (it.value().is_string()) {
                    record.set_field(it.key(), it.value().get<std::string>());
                } else if (it.value().is_number()) {
                    record.set_field(it.key(), it.value().dump());
                } else if (it.value().is_boolean()) {
                    record.set_field(it.key(), it.value().get<bool>() ? "true" : "false");
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Failed to parse JSON line: " + std::string(e.what()));
    }
    
    return record;
}
} 