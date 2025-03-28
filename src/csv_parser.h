#pragma once

#include "log_parser.h"
#include "data_loader_config.h"
#include <vector>

namespace logai {

class CsvParser : public LogParser {
public:
    explicit CsvParser(const DataLoaderConfig& config);
    ~CsvParser() noexcept override;
    
    LogEntry parse(const std::string& line) override;
    bool validate(const std::string& line) override;
    LogRecordObject parse_line(const std::string& line) override;

private:
    DataLoaderConfig config_;
    std::vector<std::string> headers_;
    char delimiter_ = ',';
    
    std::vector<std::string_view> split_line(std::string_view line, char delimiter = ',');
    std::vector<std::string> parse_csv_line(std::string_view line);
};

} 