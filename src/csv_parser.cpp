#include "std_includes.h"
#include "csv_parser.h"
#include "simd_scanner.h"
#include <iomanip>
#include "log_parser.h"
#include <regex>
#include <sstream>
#include <chrono>
#include <vector>
#include <boost/algorithm/string/trim.hpp>

namespace logai {

namespace {
    // Regular expression for parsing CSV fields
    const std::regex csv_regex{R"((?:^|,)(?:"([^"]*(?:""[^"]*)*)"|([^,]*)))"};
    
    // Split a CSV line into fields
    std::vector<std::string> splitCsvLine(const std::string& line) {
        std::vector<std::string> fields;
        std::string::const_iterator searchStart(line.cbegin());
        std::smatch match;
        
        while (std::regex_search(searchStart, line.cend(), match, csv_regex)) {
            std::string field;
            if (match[1].matched) {
                // Quoted field
                field = match[1].str();
                // Replace double quotes with single quotes
                size_t pos = 0;
                while ((pos = field.find("\"\"", pos)) != std::string::npos) {
                    field.replace(pos, 2, "\"");
                    pos += 1;
                }
            } else {
                // Unquoted field
                field = match[2].str();
            }
            boost::algorithm::trim(field);
            fields.push_back(field);
            searchStart = match.suffix().first;
        }
        
        return fields;
    }
}

CsvParser::CsvParser(const DataLoaderConfig& config) : config_(config) {}

// Helper function to parse timestamps based on format
std::optional<std::chrono::system_clock::time_point> parse_timestamp(std::string_view timestamp, const std::string& format) {
    std::tm tm = {};
    std::string timestamp_str(timestamp);
    std::istringstream ss(timestamp_str);
    
    ss >> std::get_time(&tm, format.c_str());
    
    if (ss.fail()) {
        return std::nullopt;
    }
    
    // Convert tm to time_t then to system_clock::time_point
    std::time_t time = std::mktime(&tm);
    if (time == -1) {
        return std::nullopt;
    }
    
    return std::chrono::system_clock::from_time_t(time);
}

LogRecordObject CsvParser::parse_line(std::string_view line) {
    LogRecordObject record;
    std::vector<std::string_view> fields = split_line(line);

    // Map fields to record based on config
    for (size_t i = 0; i < fields.size() && i < config_.dimensions.size(); ++i) {
        const auto& field = fields[i];
        const auto& dimension = config_.dimensions[i];

        if (dimension == "body") {
            record.body = std::string(field);
        } else if (dimension == "timestamp") {
            record.timestamp = parse_timestamp(field, config_.datetime_format);
        } else if (dimension == "severity") {
            record.severity = std::string(field);
        } else {
            // Use unordered_map for attributes instead of using vector indexing
            record.attributes.insert({dimension, std::string(field)});
        }
    }

    return record;
}

std::vector<std::string_view> CsvParser::split_line(std::string_view line, char delimiter) {
    std::vector<std::string_view> fields;
    fields.reserve(16);

    if (config_.use_simd) {
        // Use SIMD-optimized CSV parsing
        auto scanner = SimdLogScanner(line.data(), line.size());
        size_t start = 0;
        
        while (!scanner.atEnd()) {
            size_t pos = scanner.findChar(delimiter);
            if (pos == std::string::npos) {
                // Last field
                fields.push_back(std::string_view(line.data() + start, line.size() - start));
                break;
            }
            fields.push_back(std::string_view(line.data() + start, pos - start));
            start = pos + 1;
            scanner.advance(pos + 1);
        }
    } else {
        // Fallback to standard parsing
        size_t start = 0;
        size_t pos = 0;
        
        while ((pos = line.find(delimiter, start)) != std::string::npos) {
            fields.push_back(line.substr(start, pos - start));
            start = pos + 1;
        }
        fields.push_back(line.substr(start));
    }

    return fields;
}

LogParser::LogEntry CsvParser::parse(const std::string& line) {
    LogEntry entry;
    
    try {
        std::vector<std::string> fields = splitCsvLine(line);
        
        if (fields.empty()) {
            throw std::runtime_error("Empty CSV line");
        }
        
        // Try to identify fields based on header or content
        for (size_t i = 0; i < fields.size(); ++i) {
            const std::string& field = fields[i];
            
            // Skip empty fields
            if (field.empty()) {
                continue;
            }
            
            // Try to identify timestamp field
            if (entry.timestamp.empty()) {
                std::tm tm = {};
                std::istringstream ss(field);
                
                // Try various timestamp formats
                bool isTimestamp = false;
                
                // Try ISO 8601
                if (field.find('T') != std::string::npos) {
                    entry.timestamp = field;
                    isTimestamp = true;
                }
                // Try YYYY-MM-DD HH:MM:SS
                else if (ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S")) {
                    std::stringstream out;
                    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S.000Z");
                    entry.timestamp = out.str();
                    isTimestamp = true;
                }
                // Try MM/DD/YYYY HH:MM:SS
                else {
                    ss.clear();
                    ss.str(field);
                    if (ss >> std::get_time(&tm, "%m/%d/%Y %H:%M:%S")) {
                        std::stringstream out;
                        out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S.000Z");
                        entry.timestamp = out.str();
                        isTimestamp = true;
                    }
                }
                
                if (isTimestamp) {
                    continue;
                }
            }
            
            // Try to identify log level field
            if (entry.level.empty()) {
                std::string upperField = field;
                std::transform(upperField.begin(), upperField.end(), upperField.begin(), ::toupper);
                
                if (upperField == "DEBUG" || upperField == "INFO" || 
                    upperField == "WARN" || upperField == "WARNING" ||
                    upperField == "ERROR" || upperField == "FATAL" ||
                    upperField == "CRITICAL") {
                    entry.level = upperField;
                    continue;
                }
            }
            
            // Try to identify message field (usually the longest field)
            if (entry.message.empty() && field.length() > 20) {
                entry.message = field;
                continue;
            }
            
            // Store other fields with column index as key
            entry.fields["field_" + std::to_string(i)] = field;
        }
        
        // Set default values if not found
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
        
        if (entry.level.empty()) {
            entry.level = "INFO";
        }
        
        if (entry.message.empty() && !fields.empty()) {
            entry.message = fields[0];
        }
        
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse CSV line: " + std::string(e.what()));
    }
    
    return entry;
}

bool CsvParser::validate(const std::string& line) {
    try {
        std::vector<std::string> fields = splitCsvLine(line);
        return !fields.empty();
    } catch (...) {
        return false;
    }
}

} 