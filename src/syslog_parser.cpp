#include "log_parser.h"
#include <regex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <map>

namespace logai {

namespace {
    // Regular expressions for parsing syslog format
    const std::regex syslog_regex{
        R"(^(?:(\w{3}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2})|(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?))?\s*)"
        R"((?:(\w+|\d+(?:\.\d+){3})\s+)?"  // hostname/IP (optional)
        R"((?:(\w+(?:\[\d+\])?):)?\s*)"     // program[pid] (optional)
        R"((.*))$)"};                         // message

    const std::regex priority_regex{R"(<(\d{1,3})>)"};
    
    // Mapping of facility codes
    const std::map<int, std::string> facility_map = {
        {0, "kern"}, {1, "user"}, {2, "mail"}, {3, "daemon"},
        {4, "auth"}, {5, "syslog"}, {6, "lpr"}, {7, "news"},
        {8, "uucp"}, {9, "cron"}, {10, "authpriv"}, {11, "ftp"},
        {12, "ntp"}, {13, "security"}, {14, "console"}, {15, "mark"},
        {16, "local0"}, {17, "local1"}, {18, "local2"}, {19, "local3"},
        {20, "local4"}, {21, "local5"}, {22, "local6"}, {23, "local7"}
    };
    
    // Mapping of severity codes
    const std::map<int, std::string> severity_map = {
        {0, "EMERG"}, {1, "ALERT"}, {2, "CRIT"}, {3, "ERR"},
        {4, "WARNING"}, {5, "NOTICE"}, {6, "INFO"}, {7, "DEBUG"}
    };
    
    // Convert month name to number
    int monthToNumber(const std::string& month) {
        static const std::map<std::string, int> months = {
            {"Jan", 1}, {"Feb", 2}, {"Mar", 3}, {"Apr", 4},
            {"May", 5}, {"Jun", 6}, {"Jul", 7}, {"Aug", 8},
            {"Sep", 9}, {"Oct", 10}, {"Nov", 11}, {"Dec", 12}
        };
        auto it = months.find(month);
        return it != months.end() ? it->second : 1;
    }
}

LogParser::LogEntry SyslogParser::parse(const std::string& line) {
    LogEntry entry;
    std::smatch match;
    
    // First try to extract priority if present
    std::string message = line;
    if (std::regex_search(message, match, priority_regex)) {
        int priority = std::stoi(match[1]);
        int facility = priority >> 3;
        int severity = priority & 0x7;
        
        entry.fields["facility"] = facility_map.count(facility) ? 
            facility_map.at(facility) : std::to_string(facility);
        entry.level = severity_map.count(severity) ? 
            severity_map.at(severity) : std::to_string(severity);
            
        message = message.substr(match[0].length());
    }
    
    if (std::regex_match(message, match, syslog_regex)) {
        // Parse timestamp
        std::string timestamp = match[1].matched ? match[1].str() : match[2].str();
        if (!timestamp.empty()) {
            if (match[1].matched) {
                // Traditional syslog timestamp (MMM DD HH:MM:SS)
                std::string month = timestamp.substr(0, 3);
                int day = std::stoi(timestamp.substr(4, 2));
                std::string time = timestamp.substr(7);
                
                // Use current year since syslog doesn't include it
                auto now = std::chrono::system_clock::now();
                auto now_time = std::chrono::system_clock::to_time_t(now);
                auto now_tm = *std::gmtime(&now_time);
                int year = now_tm.tm_year + 1900;
                
                std::stringstream ss;
                ss << year << '-'
                   << std::setfill('0') << std::setw(2) << monthToNumber(month) << '-'
                   << std::setfill('0') << std::setw(2) << day << 'T'
                   << time << ".000Z";
                entry.timestamp = ss.str();
            } else {
                // ISO 8601 timestamp
                entry.timestamp = timestamp;
            }
        }
        
        // Parse hostname/IP
        if (match[3].matched) {
            entry.fields["host"] = match[3].str();
        }
        
        // Parse program[pid]
        if (match[4].matched) {
            std::string prog = match[4].str();
            size_t pid_start = prog.find('[');
            if (pid_start != std::string::npos) {
                entry.fields["program"] = prog.substr(0, pid_start);
                size_t pid_end = prog.find(']');
                if (pid_end != std::string::npos) {
                    entry.fields["pid"] = prog.substr(pid_start + 1, pid_end - pid_start - 1);
                }
            } else {
                entry.fields["program"] = prog;
            }
        }
        
        // Parse message
        entry.message = match[5].str();
    } else {
        // If regex doesn't match, treat entire line as message
        entry.message = message;
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
    
    return entry;
}

bool SyslogParser::validate(const std::string& line) {
    std::smatch match;
    std::string message = line;
    
    // Check for priority
    if (std::regex_search(message, match, priority_regex)) {
        message = message.substr(match[0].length());
    }
    
    // Check if remaining message matches syslog format
    return std::regex_match(message, syslog_regex);
}

} // namespace logai 