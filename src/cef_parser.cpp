#include "log_parser.h"
#include <regex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <map>
#include <boost/algorithm/string/predicate.hpp>

namespace logai {

namespace {
    // Regular expressions for parsing CEF format
    const std::regex cef_header_regex{
        R"(CEF:(\d+)\|([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|(.*))"};
    
    const std::regex cef_extension_regex{
        R"((\w+)=(?:([^=\s]+)|"([^"]*)")(?:\s+|$))"};
        
    // Mapping of CEF severity levels to standard log levels
    const std::map<std::string, std::string> severity_map = {
        {"0", "INFO"},     // Unknown
        {"1", "INFO"},     // Low
        {"2", "INFO"},     // Low
        {"3", "INFO"},     // Low
        {"4", "WARNING"},  // Medium
        {"5", "WARNING"},  // Medium
        {"6", "WARNING"},  // Medium
        {"7", "ERROR"},    // High
        {"8", "ERROR"},    // High
        {"9", "ERROR"},    // High
        {"10", "FATAL"}    // Very-High
    };
}

LogParser::LogEntry CefParser::parse(const std::string& line) {
    LogEntry entry;
    std::smatch match;
    
    if (std::regex_match(line, match, cef_header_regex)) {
        // Parse CEF version
        entry.fields["cef_version"] = match[1].str();
        
        // Parse device vendor
        entry.fields["device_vendor"] = match[2].str();
        
        // Parse device product
        entry.fields["device_product"] = match[3].str();
        
        // Parse device version
        entry.fields["device_version"] = match[4].str();
        
        // Parse signature ID
        entry.fields["signature_id"] = match[5].str();
        
        // Parse name (becomes the message)
        entry.message = match[6].str();
        
        // Parse severity
        std::string severity = match[7].str();
        entry.fields["severity"] = severity;
        entry.level = severity_map.count(severity) ? 
            severity_map.at(severity) : "INFO";
        
        // Parse extension fields
        std::string extensions = match[8].str();
        std::string::const_iterator searchStart(extensions.cbegin());
        
        while (std::regex_search(searchStart, extensions.cend(), match, cef_extension_regex)) {
            std::string key = match[1].str();
            std::string value = match[2].length() > 0 ? match[2].str() : match[3].str();
            
            // Handle special fields
            if (key == "rt" || key == "deviceCustomDate1") {
                // CEF timestamp field
                entry.timestamp = value;
            } else if (key == "msg") {
                // Additional message information
                if (!entry.message.empty()) {
                    entry.message += " - ";
                }
                entry.message += value;
            } else {
                // Store all other fields
                entry.fields[key] = value;
            }
            
            searchStart = match.suffix().first;
        }
        
    } else {
        // If regex doesn't match, treat entire line as message
        entry.message = line;
        entry.level = "INFO";
    }
    
    // Set default timestamp if not set
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
    
    // Try to convert timestamp to ISO 8601 if it's in a different format
    if (entry.timestamp.find('T') == std::string::npos) {
        try {
            // Attempt to parse various common timestamp formats
            std::tm tm = {};
            std::istringstream ss(entry.timestamp);
            
            // Try MMM dd yyyy HH:mm:ss
            if (ss >> std::get_time(&tm, "%b %d %Y %H:%M:%S")) {
                std::stringstream out;
                out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S.000Z");
                entry.timestamp = out.str();
            }
            // Add more timestamp format parsing as needed
        } catch (...) {
            // Keep original timestamp if parsing fails
        }
    }
    
    return entry;
}

bool CefParser::validate(const std::string& line) {
    return boost::starts_with(line, "CEF:") && 
           std::count(line.begin(), line.end(), '|') >= 7;
}

} // namespace logai 