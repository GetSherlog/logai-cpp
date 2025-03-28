#pragma once

#include "log_parser.h"
#include "data_loader_config.h"
#include <regex>

namespace logai {

class RegexParser : public LogParser {
public:
    RegexParser(const DataLoaderConfig& config, const std::string& pattern);
    
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
    LogRecordObject parse_line(const std::string& line) override;

private:
    const DataLoaderConfig& config_;
    std::regex pattern_;
};
}