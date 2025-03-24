#include "log_parser.h"
#include <regex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <boost/algorithm/string/trim.hpp>

namespace logai {

namespace {
    // Regular expressions for parsing Log4j format
    const std::regex log4j_regex{
        R"(^(?:(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}(?:,\d{3})?)\s+)"  // timestamp
        R"((\w+)\s+)"                                                      // level
        R"(?:\[([^\]]+)\]\s+)?)"                                          // thread (optional)
        R"((?:([^\s:]+):\s+)?)"                                          // logger (optional)
        R"((.*))$)"};                                                      // message

    const std::regex kv_regex{R"((\w+)=(?:([^"\s][^\s]*)|"([^"]*)"))"};
}

LogParser::LogEntry Log4jParser::parse(const std::string& line) {
    LogEntry entry;
    std::smatch match;
    
    if (std::regex_match(line, match, log4j_regex)) {
        // Parse timestamp
        if (match[1].matched) {
            std::string ts = match[1].str();
            // Convert space to T and add Z for ISO 8601
            ts[10] = 'T';
            if (ts.find(',') != std::string::npos) {
                // Convert ,nnn to .nnn
                ts[19] = '.';
            }
            entry.timestamp = ts + (ts.length() > 23 ? "Z" : ".000Z");
        }
        
        // Parse level
        if (match[2].matched) {
            entry.level = match[2].str();
        }
        
        // Parse thread name
        if (match[3].matched) {
            entry.fields["thread"] = match[3].str();
        }
        
        // Parse logger name
        if (match[4].matched) {
            entry.fields["logger"] = match[4].str();
        }
        
        // Parse message
        if (match[5].matched) {
            entry.message = match[5].str();
        }
        
    } else {
        // If regex doesn't match, treat entire line as message
        entry.message = line;
    }
    
    // Set default level if not set
    if (entry.level.empty()) {
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
    
    // Try to extract additional key=value pairs from the message
    std::string msg = entry.message;
    std::smatch kvMatch;
    
    std::string::const_iterator searchStart(msg.cbegin());
    bool foundKV = false;
    
    while (std::regex_search(searchStart, msg.cend(), kvMatch, kv_regex)) {
        foundKV = true;
        std::string key = kvMatch[1].str();
        std::string value = kvMatch[2].length() > 0 ? kvMatch[2].str() : kvMatch[3].str();
        
        if (key != "thread" && key != "logger") {  // Don't overwrite existing fields
            entry.fields[key] = value;
        }
        
        searchStart = kvMatch.suffix().first;
    }
    
    // If we found key=value pairs, update the message to exclude them
    if (foundKV) {
        size_t lastKVPos = msg.find_last_of('}');
        if (lastKVPos != std::string::npos) {
            entry.message = msg.substr(0, msg.find_first_of('{'));
            boost::algorithm::trim(entry.message);
        }
    }
    
    return entry;
}

bool Log4jParser::validate(const std::string& line) {
    std::smatch match;
    return std::regex_match(line, match, log4j_regex);
}

} // namespace logai 