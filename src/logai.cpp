#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include "file_data_loader.h"
#include "log_parser.h"

using json = nlohmann::json;

class LogAI {
public:
    enum class InputFormat {
        LOGFMT,  // default
        JSONL,
        JSON,
        CSV,
        LINE,
        SYSLOG,
        LOG4J,
        CEF,
        UNIX,
        RFC5424
    };

    enum class OutputFormat {
        LOGFMT,  // default
        JSONL,
        JSON,
        CSV,
        TSV,
        PSV
    };

    struct Options {
        InputFormat input_format = InputFormat::LOGFMT;
        OutputFormat output_format = OutputFormat::LOGFMT;
        std::string input_encoding = "utf-8";
        std::string input_delimiter = ",";
        bool no_header = false;
        bool logical_lines = false;
        std::vector<std::string> keys;
        std::vector<std::string> exclude_keys;
        std::vector<std::string> log_levels;
        std::vector<std::string> exclude_log_levels;
        std::string since;
        std::string until;
        std::string grep_pattern;
        bool grep_case_sensitive = true;
        bool stats_only = false;
        bool follow = false;
        bool color = true;
    };

    void process(const std::string& file_path, const Options& opts) {
        std::cout << "Processing log file: " << file_path << std::endl;
        
        // Configure file loader
        FileDataLoaderConfig loader_config;
        loader_config.encoding = opts.input_encoding;
        loader_config.delimiter = opts.input_delimiter;
        loader_config.has_header = !opts.no_header;
        loader_config.logical_lines = opts.logical_lines;
        loader_config.format = formatToString(opts.input_format);

        FileDataLoader loader(file_path, loader_config);
        
        if (opts.follow) {
            processStreamingLogs(loader, opts);
        } else {
            processStaticLogs(loader, opts);
        }
    }

private:
    void processStreamingLogs(FileDataLoader& loader, const Options& opts) {
        std::cout << "Following log file..." << std::endl;
        
        loader.streamData([this, &opts](const LogParser::LogEntry& entry) {
            processLogEntry(entry, opts);
            return true;  // continue streaming
        });
    }

    void processStaticLogs(FileDataLoader& loader, const Options& opts) {
        std::vector<LogParser::LogEntry> entries;
        loader.loadData(entries);

        std::cout << "Found " << entries.size() << " log entries" << std::endl;
        
        if (opts.stats_only) {
            showStats(entries, opts);
            return;
        }

        for (const auto& entry : entries) {
            processLogEntry(entry, opts);
        }
    }

    void processLogEntry(const LogParser::LogEntry& entry, const Options& opts) {
        // Apply filters
        if (!passesFilters(entry, opts)) {
            return;
        }

        // Format output
        outputLogEntry(entry, opts);
    }

    bool passesFilters(const LogParser::LogEntry& entry, const Options& opts) {
        // Log level filter
        if (!opts.log_levels.empty() && 
            std::find(opts.log_levels.begin(), opts.log_levels.end(), entry.level) == opts.log_levels.end()) {
            return false;
        }
        
        if (!opts.exclude_log_levels.empty() && 
            std::find(opts.exclude_log_levels.begin(), opts.exclude_log_levels.end(), entry.level) != opts.exclude_log_levels.end()) {
            return false;
        }

        // Time filter
        if (!opts.since.empty() || !opts.until.empty()) {
            // TODO: Implement time filtering
        }

        // Grep filter
        if (!opts.grep_pattern.empty()) {
            std::regex pattern(opts.grep_pattern, 
                opts.grep_case_sensitive ? std::regex::ECMAScript : std::regex::icase);
            if (!std::regex_search(entry.message, pattern)) {
                return false;
            }
        }

        return true;
    }

    void outputLogEntry(const LogParser::LogEntry& entry, const Options& opts) {
        switch (opts.output_format) {
            case OutputFormat::LOGFMT:
                outputLogfmt(entry, opts);
                break;
            case OutputFormat::JSONL:
                outputJsonl(entry, opts);
                break;
            case OutputFormat::JSON:
                outputJson(entry, opts);
                break;
            case OutputFormat::CSV:
                outputCsv(entry, opts);
                break;
            case OutputFormat::TSV:
                outputTsv(entry, opts);
                break;
            case OutputFormat::PSV:
                outputPsv(entry, opts);
                break;
        }
    }

    void outputLogfmt(const LogParser::LogEntry& entry, const Options& opts) {
        std::stringstream ss;
        
        // Only output requested keys
        if (!opts.keys.empty()) {
            for (const auto& key : opts.keys) {
                if (key == "timestamp") ss << "timestamp=" << entry.timestamp << " ";
                else if (key == "level") ss << "level=" << entry.level << " ";
                else if (key == "message") ss << "message=" << entry.message << " ";
                else if (entry.fields.count(key)) ss << key << "=" << entry.fields.at(key) << " ";
            }
        } else {
            ss << "timestamp=" << entry.timestamp << " ";
            ss << "level=" << entry.level << " ";
            ss << "message=" << entry.message;
            for (const auto& [key, value] : entry.fields) {
                if (opts.exclude_keys.empty() || 
                    std::find(opts.exclude_keys.begin(), opts.exclude_keys.end(), key) == opts.exclude_keys.end()) {
                    ss << " " << key << "=" << value;
                }
            }
        }

        if (opts.color) {
            // Add color based on log level
            if (entry.level == "ERROR" || entry.level == "FATAL") {
                std::cout << "\033[1;31m" << ss.str() << "\033[0m" << std::endl;
            } else if (entry.level == "WARN" || entry.level == "WARNING") {
                std::cout << "\033[1;33m" << ss.str() << "\033[0m" << std::endl;
            } else {
                std::cout << ss.str() << std::endl;
            }
        } else {
            std::cout << ss.str() << std::endl;
        }
    }

    void outputJsonl(const LogParser::LogEntry& entry, const Options& opts) {
        json j;
        
        if (opts.keys.empty()) {
            j["timestamp"] = entry.timestamp;
            j["level"] = entry.level;
            j["message"] = entry.message;
            for (const auto& [key, value] : entry.fields) {
                if (opts.exclude_keys.empty() || 
                    std::find(opts.exclude_keys.begin(), opts.exclude_keys.end(), key) == opts.exclude_keys.end()) {
                    j[key] = value;
                }
            }
        } else {
            for (const auto& key : opts.keys) {
                if (key == "timestamp") j["timestamp"] = entry.timestamp;
                else if (key == "level") j["level"] = entry.level;
                else if (key == "message") j["message"] = entry.message;
                else if (entry.fields.count(key)) j[key] = entry.fields.at(key);
            }
        }

        std::cout << j.dump() << std::endl;
    }

    void outputJson(const LogParser::LogEntry& entry, const Options& opts) {
        // TODO: Implement JSON output (array of entries)
    }

    void outputCsv(const LogParser::LogEntry& entry, const Options& opts) {
        // TODO: Implement CSV output
    }

    void outputTsv(const LogParser::LogEntry& entry, const Options& opts) {
        // TODO: Implement TSV output
    }

    void outputPsv(const LogParser::LogEntry& entry, const Options& opts) {
        // TODO: Implement PSV output
    }

    void showStats(const std::vector<LogParser::LogEntry>& entries, const Options& opts) {
        std::cout << "\nLog Statistics:" << std::endl;
        std::cout << "---------------" << std::endl;
        std::cout << "Total entries: " << entries.size() << std::endl;

        // Time span
        if (!entries.empty()) {
            std::cout << "\nTime span: " << entries.front().timestamp 
                     << " to " << entries.back().timestamp << std::endl;
        }

        // Log levels distribution
        std::unordered_map<std::string, size_t> level_counts;
        for (const auto& entry : entries) {
            level_counts[entry.level]++;
        }

        std::cout << "\nLog levels:" << std::endl;
        for (const auto& [level, count] : level_counts) {
            std::cout << "  " << level << ": " << count << std::endl;
        }

        // Fields found
        std::set<std::string> unique_fields;
        for (const auto& entry : entries) {
            for (const auto& [key, _] : entry.fields) {
                unique_fields.insert(key);
            }
        }

        std::cout << "\nFields found:" << std::endl;
        for (const auto& field : unique_fields) {
            std::cout << "  " << field << std::endl;
        }
    }

    std::string formatToString(InputFormat format) {
        switch (format) {
            case InputFormat::LOGFMT: return "logfmt";
            case InputFormat::JSONL: return "jsonl";
            case InputFormat::JSON: return "json";
            case InputFormat::CSV: return "csv";
            case InputFormat::LINE: return "line";
            case InputFormat::SYSLOG: return "syslog";
            case InputFormat::LOG4J: return "log4j";
            case InputFormat::CEF: return "cef";
            case InputFormat::UNIX: return "unix";
            case InputFormat::RFC5424: return "rfc5424";
            default: return "logfmt";
        }
    }
};

int main(int argc, char* argv[]) {
    cxxopts::Options options("logai", "AI-powered log analysis tool");
    
    options.add_options()
        ("f,format", "Input format (logfmt,jsonl,json,csv,line,syslog,log4j,cef)", cxxopts::value<std::string>()->default_value("logfmt"))
        ("F,output-format", "Output format (logfmt,jsonl,json,csv,tsv,psv)", cxxopts::value<std::string>()->default_value("logfmt"))
        ("k,keys", "Only show specific keys", cxxopts::value<std::vector<std::string>>())
        ("K,keys-not", "Exclude specific keys", cxxopts::value<std::vector<std::string>>())
        ("l,loglevels", "Filter by log levels", cxxopts::value<std::vector<std::string>>())
        ("L,not-loglevels", "Exclude log levels", cxxopts::value<std::vector<std::string>>())
        ("since", "Show logs since timestamp/duration", cxxopts::value<std::string>())
        ("until", "Show logs until timestamp/duration", cxxopts::value<std::string>())
        ("g,grep", "Filter logs by regex pattern", cxxopts::value<std::string>())
        ("i,ignore-case", "Case insensitive grep", cxxopts::value<bool>()->default_value("false"))
        ("n,follow", "Follow log file (like tail -f)", cxxopts::value<bool>()->default_value("false"))
        ("S,stats-only", "Only show statistics", cxxopts::value<bool>()->default_value("false"))
        ("no-color", "Disable color output", cxxopts::value<bool>()->default_value("false"))
        ("input-encoding", "Input file encoding", cxxopts::value<std::string>()->default_value("utf-8"))
        ("input-delimiter", "Input field delimiter for CSV/TSV", cxxopts::value<std::string>()->default_value(","))
        ("no-header", "Input has no header row", cxxopts::value<bool>()->default_value("false"))
        ("logical-lines", "Handle multi-line logs", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage")
        ("input", "Input file", cxxopts::value<std::string>());

    options.parse_positional({"input"});
    options.positional_help("[input]");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!result.count("input")) {
        std::cerr << "Error: Input file required" << std::endl;
        return 1;
    }

    LogAI logai;
    LogAI::Options opts;

    // Parse command line options into LogAI options
    if (result.count("format")) {
        std::string fmt = result["format"].as<std::string>();
        if (fmt == "logfmt") opts.input_format = LogAI::InputFormat::LOGFMT;
        else if (fmt == "jsonl") opts.input_format = LogAI::InputFormat::JSONL;
        else if (fmt == "json") opts.input_format = LogAI::InputFormat::JSON;
        else if (fmt == "csv") opts.input_format = LogAI::InputFormat::CSV;
        else if (fmt == "line") opts.input_format = LogAI::InputFormat::LINE;
        else if (fmt == "syslog") opts.input_format = LogAI::InputFormat::SYSLOG;
        else if (fmt == "log4j") opts.input_format = LogAI::InputFormat::LOG4J;
        else if (fmt == "cef") opts.input_format = LogAI::InputFormat::CEF;
        else if (fmt == "unix") opts.input_format = LogAI::InputFormat::UNIX;
        else if (fmt == "rfc5424") opts.input_format = LogAI::InputFormat::RFC5424;
        else {
            std::cerr << "Error: Unsupported input format: " << fmt << std::endl;
            return 1;
        }
    }
    
    if (result.count("output-format")) {
        std::string fmt = result["output-format"].as<std::string>();
        if (fmt == "logfmt") opts.output_format = LogAI::OutputFormat::LOGFMT;
        else if (fmt == "jsonl") opts.output_format = LogAI::OutputFormat::JSONL;
        else if (fmt == "json") opts.output_format = LogAI::OutputFormat::JSON;
        else if (fmt == "csv") opts.output_format = LogAI::OutputFormat::CSV;
        else if (fmt == "tsv") opts.output_format = LogAI::OutputFormat::TSV;
        else if (fmt == "psv") opts.output_format = LogAI::OutputFormat::PSV;
        else {
            std::cerr << "Error: Unsupported output format: " << fmt << std::endl;
            return 1;
        }
    }

    if (result.count("keys")) {
        opts.keys = result["keys"].as<std::vector<std::string>>();
    }

    if (result.count("keys-not")) {
        opts.exclude_keys = result["keys-not"].as<std::vector<std::string>>();
    }

    if (result.count("loglevels")) {
        opts.log_levels = result["loglevels"].as<std::vector<std::string>>();
    }

    if (result.count("not-loglevels")) {
        opts.exclude_log_levels = result["not-loglevels"].as<std::vector<std::string>>();
    }

    if (result.count("since")) {
        opts.since = result["since"].as<std::string>();
    }

    if (result.count("until")) {
        opts.until = result["until"].as<std::string>();
    }

    if (result.count("grep")) {
        opts.grep_pattern = result["grep"].as<std::string>();
        opts.grep_case_sensitive = !result["ignore-case"].as<bool>();
    }

    opts.follow = result["follow"].as<bool>();
    opts.stats_only = result["stats-only"].as<bool>();
    opts.color = !result["no-color"].as<bool>();
    opts.input_encoding = result["input-encoding"].as<std::string>();
    opts.input_delimiter = result["input-delimiter"].as<std::string>();
    opts.no_header = result["no-header"].as<bool>();
    opts.logical_lines = result["logical-lines"].as<bool>();

    try {
        logai.process(result["input"].as<std::string>(), opts);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 