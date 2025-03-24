#include "log_parser.h"
#include <regex>
#include <sstream>
#include <boost/algorithm/string.hpp>

namespace logai {

namespace {
    // Regular expressions for parsing logfmt
    const std::regex kv_regex(R"(([^=\s]+)=(?:([^"\s][^\s]*)|"([^"]*)")");
    const std::regex quoted_value_regex(R"("([^"]*)")");
    const std::regex unquoted_value_regex(R"([^"\s][^\s]*)");
}

LogParser::LogEntry LogfmtParser::parse(const std::string& line) {
    LogEntry entry;
    
    // Match all key-value pairs
    auto begin = std::sregex_iterator(line.begin(), line.end(), kv_regex);
    auto end = std::sregex_iterator();

    for (std::sregex_iterator i = begin; i != end; ++i) {
        const std::smatch& match = *i;
        std::string key = match[1].str();
        
        // Value is either in group 2 (unquoted) or group 3 (quoted)
        std::string value = match[2].length() > 0 ? match[2].str() : match[3].str();

        // Handle special fields
        if (key == "time" || key == "timestamp" || key == "ts" || key == "at") {
            entry.timestamp = value;
        }
        else if (key == "level" || key == "severity" || key == "loglevel") {
            entry.level = value;
        }
        else if (key == "msg" || key == "message") {
            entry.message = value;
        }
        else {
            entry.fields[key] = value;
        }
    }

    // If no explicit message field was found, use the remaining text
    if (entry.message.empty()) {
        std::string remaining;
        size_t pos = 0;
        
        // Find position after last key-value pair
        for (const auto& match : std::vector<std::smatch>(begin, end)) {
            pos = std::max(pos, match.position() + match.length());
        }

        if (pos < line.length()) {
            remaining = line.substr(pos);
            boost::trim(remaining);
            if (!remaining.empty()) {
                entry.message = remaining;
            }
        }
    }

    return entry;
}

bool LogfmtParser::validate(const std::string& line) {
    // Basic validation - check if there's at least one key=value pair
    return std::regex_search(line, kv_regex);
}

std::pair<std::string, std::string> LogfmtParser::parseKeyValue(const std::string& pair) {
    auto pos = pair.find('=');
    if (pos == std::string::npos) {
        return {"", ""};
    }

    std::string key = pair.substr(0, pos);
    std::string value = pair.substr(pos + 1);
    
    // Handle quoted values
    if (!value.empty() && value[0] == '"') {
        std::smatch match;
        if (std::regex_match(value, match, quoted_value_regex)) {
            value = match[1].str();
        }
    }
    else {
        std::smatch match;
        if (std::regex_match(value, match, unquoted_value_regex)) {
            value = match[0].str();
        }
    }

    return {key, value};
}

std::unique_ptr<LogParser> LogParserFactory::create(const std::string& format) {
    if (format == "logfmt") {
        return std::make_unique<LogfmtParser>();
    }
    else if (format == "jsonl") {
        return std::make_unique<JsonlParser>();
    }
    else if (format == "syslog") {
        return std::make_unique<SyslogParser>();
    }
    else if (format == "log4j") {
        return std::make_unique<Log4jParser>();
    }
    else if (format == "cef") {
        return std::make_unique<CefParser>();
    }
    else if (format == "line") {
        return std::make_unique<LineParser>();
    }
    else {
        throw std::runtime_error("Unsupported log format: " + format);
    }
}

} // namespace logai 