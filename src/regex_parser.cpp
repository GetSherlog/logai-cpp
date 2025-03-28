#include "regex_parser.h"
#include <stdexcept>

namespace logai {

// Add declaration for parse_timestamp function
std::optional<std::chrono::system_clock::time_point> parse_timestamp(std::string_view timestamp, const std::string& format);

RegexParser::RegexParser(const DataLoaderConfig& config, const std::string& pattern)
    : config_(config), pattern_(pattern) {}

LogEntry RegexParser::parse(const std::string& line) {
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

bool RegexParser::validate(const std::string& line) {
    try {
        std::smatch matches;
        return std::regex_match(line, matches, pattern_);
    } catch (const std::regex_error&) {
        return false;
    }
}

LogRecordObject RegexParser::parse_line(const std::string& line) {
    LogRecordObject record;
    std::string line_str(line);  // Convert to string for regex
    std::smatch matches;
    
    try {
        if (!std::regex_match(line_str, matches, pattern_)) {
            throw std::runtime_error("Line does not match pattern");
        }

        // Map capture groups to record fields
        for (size_t i = 1; i < matches.size(); ++i) {
            const auto& match = matches[i];
            // Use index as name since std::regex doesn't support named groups
            const auto name = std::to_string(i);

            if (name == "body") {
                record.body = match.str();
            } else if (name == "timestamp") {
                record.timestamp = parse_timestamp(match.str(), config_.datetime_format);
            } else if (name == "severity") {
                record.severity = match.str();
            } else {
                record.attributes[name] = match.str();
            }
        }
    } catch (const std::regex_error& e) {
        throw std::runtime_error("Regex error: " + std::string(e.what()));
    }

    return record;
}
} 