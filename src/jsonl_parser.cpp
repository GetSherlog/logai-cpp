#include "log_parser.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

using json = nlohmann::json;

namespace logai {

LogParser::LogEntry JsonlParser::parse(const std::string& line) {
    LogEntry entry;
    
    try {
        json j = json::parse(line);
        
        // Look for timestamp field with various common names
        for (const auto& ts_field : {"time", "timestamp", "ts", "@timestamp", "datetime"}) {
            if (j.contains(ts_field)) {
                entry.timestamp = j[ts_field].get<std::string>();
                break;
            }
        }
        
        // Look for level field with various common names
        for (const auto& level_field : {"level", "severity", "loglevel", "@level"}) {
            if (j.contains(level_field)) {
                entry.level = j[level_field].get<std::string>();
                break;
            }
        }
        
        // Look for message field with various common names
        for (const auto& msg_field : {"msg", "message", "@message", "log"}) {
            if (j.contains(msg_field)) {
                entry.message = j[msg_field].get<std::string>();
                break;
            }
        }
        
        // Add all other fields to the fields map
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string& key = it.key();
            
            // Skip fields we already processed
            if (key == "time" || key == "timestamp" || key == "ts" || key == "@timestamp" || key == "datetime" ||
                key == "level" || key == "severity" || key == "loglevel" || key == "@level" ||
                key == "msg" || key == "message" || key == "@message" || key == "log") {
                continue;
            }
            
            // Handle different value types
            if (it.value().is_string()) {
                entry.fields[key] = it.value().get<std::string>();
            } else if (it.value().is_number()) {
                entry.fields[key] = it.value().dump();
            } else if (it.value().is_boolean()) {
                entry.fields[key] = it.value().get<bool>() ? "true" : "false";
            } else if (it.value().is_null()) {
                entry.fields[key] = "null";
            } else {
                // For objects and arrays, store JSON string representation
                entry.fields[key] = it.value().dump();
            }
        }
        
        // If no timestamp found, use current time
        if (entry.timestamp.empty()) {
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;
            
            std::stringstream ss;
            ss << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%dT%H:%M:%S")
               << '.' << std::setfill('0') << std::setw(3) << now_ms
               << 'Z';
            
            entry.timestamp = ss.str();
        }
        
        // If no level found, default to INFO
        if (entry.level.empty()) {
            entry.level = "INFO";
        }
        
        // If no message found but we have a complete JSON object, use that
        if (entry.message.empty()) {
            entry.message = line;
        }
        
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse JSONL line: " + std::string(e.what()));
    }
    
    return entry;
}

bool JsonlParser::validate(const std::string& line) {
    try {
        json::parse(line);
        return true;
    } catch (const json::parse_error&) {
        return false;
    }
}

} // namespace logai 