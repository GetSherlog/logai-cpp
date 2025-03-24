#include "log_parser.h"
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace logai {

LogParser::LogEntry LineParser::parse(const std::string& line) {
    LogEntry entry;
    
    // For plain text logs, we just store the entire line as the message
    entry.message = line;
    
    // Try to set a timestamp using current time
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << now_ms
       << 'Z';
    
    entry.timestamp = ss.str();
    
    // Default level to INFO
    entry.level = "INFO";
    
    return entry;
}

bool LineParser::validate(const std::string& line) {
    // For plain text, any non-empty line is valid
    return !line.empty();
}

} // namespace logai 