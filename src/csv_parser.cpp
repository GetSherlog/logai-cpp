#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "csv_parser.h"
#include "simd_scanner.h"
#include <iomanip>
#include "log_parser.h"
#include <regex>
#include <sstream>
#include <boost/algorithm/string/trim.hpp>

namespace logai {

namespace {
    // Regular expression for parsing CSV fields
    const std::regex csv_regex("(?:^|,)(?:\"([^\"]*(?:\"\"[^\"]*)*)\"|(.*?))(?=,|$)");
    
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

CsvParser::~CsvParser() noexcept = default;

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

LogRecordObject CsvParser::parse_line(const std::string&  line) {
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
            // Use set_field method instead of attributes map
            record.set_field(dimension, std::string(field));
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
    LogParser::LogEntry entry;
    try {
        // Use the existing parse_line implementation to get the LogRecordObject
        LogRecordObject record = parse_line(line);
        
        // Convert LogRecordObject to LogEntry
        entry.timestamp = record.timestamp ? std::to_string(record.timestamp->time_since_epoch().count()) : "";
        entry.level = record.severity.value_or("");
        entry.message = record.body;
        
        // Copy fields to entry.fields
        for (const auto& [key, value] : record.fields) {
            entry.fields[key.toStdString()] = value.toStdString();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse CSV line: " + std::string(e.what()));
    }
    
    return entry;
}

bool CsvParser::validate(const std::string& line) {
    try {
        // Try to parse the line to validate it
        parse_line(line);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} 