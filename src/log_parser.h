#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>
#include <folly/FBString.h>

namespace logai {

/**
 * @brief Common interface for all log parsers
 */
class LogParser {
public:
    struct LogEntry {
        std::string timestamp;
        std::string level;
        std::string message;
        std::unordered_map<std::string, std::string> fields;

        LogRecordObject to_record_object() const {
            LogRecordObject record;
            for (const auto& [key, value] : fields) {
                record.fields[folly::fbstring(key)] = folly::fbstring(value);
            }
            return record;
        }
    };

    virtual ~LogParser() = default;
    virtual LogEntry parse(const std::string& line) = 0;
    virtual bool validate(const std::string& line) = 0;

    virtual LogRecordObject parse_line(const std::string& line) {
        LogEntry entry = parse(line);
        return entry.to_record_object();
    }
};

/**
 * @brief Parser for logfmt format
 * Example: time=2024-03-24T10:15:30Z level=info msg="User logged in" user=alice
 */
class LogfmtParser : public LogParser {
public:
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
private:
    std::pair<std::string, std::string> parseKeyValue(const std::string& pair);
};

/**
 * @brief Parser for JSON Lines format
 * Example: {"time": "2024-03-24T10:15:30Z", "level": "info", "msg": "User logged in", "user": "alice"}
 */
class JsonlParser : public LogParser {
public:
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
};

/**
 * @brief Parser for syslog format
 * Example: Mar 24 10:15:30 myhost app[1234]: User logged in
 */
class SyslogParser : public LogParser {
public:
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
private:
    std::chrono::system_clock::time_point parseTimestamp(const std::string& ts);
};

/**
 * @brief Parser for Log4j format
 * Example: 2024-03-24 10:15:30,123 INFO [main] com.example.App: User logged in
 */
class Log4jParser : public LogParser {
public:
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
};

/**
 * @brief Parser for Common Event Format (CEF)
 * Example: CEF:0|vendor|product|1.0|id|name|severity|msg=User logged in
 */
class CefParser : public LogParser {
public:
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
};

/**
 * @brief Parser for plain text format
 * Just treats each line as a message
 */
class LineParser : public LogParser {
public:
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
};

/**
 * @brief Factory for creating parser instances
 */
class LogParserFactory {
public:
    static std::unique_ptr<LogParser> create(const std::string& format);
};

} // namespace logai 