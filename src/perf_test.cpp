/**
 * Performance test for LogAI-CPP
 * Tests log parsing and Parquet export with timing measurements
 */

#include "file_data_loader.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/result.h>
#include <parquet/arrow/writer.h>

using namespace logai;
using namespace std::chrono;

// Helper function to create a sample log file if none exists
void create_sample_log_file(const std::string& filepath, size_t num_lines) {
    std::ofstream logfile(filepath);
    
    // Create a CSV log file with timestamp, level, message columns
    logfile << "timestamp,level,message" << std::endl;
    
    // Generate sample log entries
    for (size_t i = 0; i < num_lines; i++) {
        logfile << "2023-10-" << std::setw(2) << std::setfill('0') << (i % 30 + 1) 
                << "T" << std::setw(2) << std::setfill('0') << (i % 24) 
                << ":00:00Z,";
        
        // Alternate log levels
        if (i % 5 == 0)
            logfile << "ERROR,";
        else if (i % 3 == 0)
            logfile << "WARNING,";
        else
            logfile << "INFO,";
        
        logfile << "This is log message #" << i << " with some sample content for testing." << std::endl;
    }
    
    logfile.close();
    std::cout << "Created sample log file with " << num_lines << " lines at " << filepath << std::endl;
}

// Helper function to detect log file format
std::string detect_log_format(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    
    // Read the first line
    std::string first_line;
    if (!std::getline(file, first_line)) {
        throw std::runtime_error("File is empty: " + filepath);
    }
    
    // Check if the file path contains HDFS
    if (filepath.find("HDFS") != std::string::npos) {
        // Check the format of the first line (basic validation for HDFS logs)
        std::regex hdfs_regex("\\d{6}\\s+\\d{6}\\s+\\w+\\s+\\w+\\s+.+");
        if (std::regex_match(first_line, hdfs_regex)) {
            return "HDFS";
        }
    }
    
    // Default: Check if the first line contains comma separators (CSV)
    if (first_line.find(',') != std::string::npos) {
        return "CSV";
    }
    
    // Default to generic log format if can't determine
    return "LOG";
}

// Configure data loader based on log format
DataLoaderConfig configure_loader(const std::string& filepath, const std::string& format, const std::string& parser_type = "") {
    DataLoaderConfig config;
    config.file_path = filepath;
    config.use_memory_mapping = true;
    config.use_simd = true;
    
    // If a specific parser type is provided, use it
    if (!parser_type.empty()) {
        std::string parser_type_lower = parser_type;
        std::transform(parser_type_lower.begin(), parser_type_lower.end(), parser_type_lower.begin(), 
                      [](unsigned char c){ return std::tolower(c); });
        
        config.log_type = parser_type_lower;
        std::cout << "Using specified parser type: " << parser_type_lower << std::endl;
        
        // Set appropriate pattern for regex or drain parser
        if (parser_type_lower == "regex" || parser_type_lower == "drain") {
            if (format == "HDFS") {
                config.log_pattern = "(\\d{6})\\s+(\\d{6})\\s+(\\w+)\\s+(\\w+)\\s+(.+)";
                config.dimensions = {"date", "time", "pid", "level", "content"};
                config.datetime_format = "%y%m%d %H%M%S";
                config.infer_datetime = true;
            } else {
                // Default pattern for other formats
                config.log_pattern = "(.+)";
            }
        }
        
        return config;
    }
    
    // Otherwise, auto-detect based on format
    if (format == "HDFS") {
        // HDFS log format configuration
        config.log_type = "regex";
        config.log_pattern = "(\\d{6})\\s+(\\d{6})\\s+(\\w+)\\s+(\\w+)\\s+(.+)";
        config.dimensions = {"date", "time", "pid", "level", "content"};
        config.datetime_format = "%y%m%d %H%M%S";
        config.infer_datetime = true;
        std::cout << "Detected HDFS log format" << std::endl;
    } else if (format == "CSV") {
        // CSV format configuration
        config.log_type = "csv";
        config.dimensions = {"timestamp", "level", "message"};
        config.datetime_format = "%Y-%m-%dT%H:%M:%SZ";
        std::cout << "Detected CSV log format" << std::endl;
    } else {
        // Default to regex parser for unknown formats
        config.log_type = "regex";
        config.log_pattern = "(.+)";
        std::cout << "Using default log format" << std::endl;
    }
    
    return config;
}

void print_timing(const std::string& operation, high_resolution_clock::duration duration) {
    auto ms = duration_cast<milliseconds>(duration).count();
    auto s = duration_cast<seconds>(duration).count();
    
    std::cout << operation << " took ";
    if (s > 0) {
        std::cout << s << "." << (ms % 1000) << " seconds" << std::endl;
    } else {
        std::cout << ms << " milliseconds" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        std::string log_filepath = "sample.log";
        std::string parquet_filepath = "output.parquet";
        size_t num_lines = 10000;
        std::string parser_type = "";
        
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "--input" && i + 1 < argc) {
                log_filepath = argv[++i];
            } else if (arg == "--output" && i + 1 < argc) {
                parquet_filepath = argv[++i];
            } else if (arg == "--lines" && i + 1 < argc) {
                num_lines = std::stoul(argv[++i]);
            } else if (arg == "--parser" && i + 1 < argc) {
                parser_type = argv[++i];
            } else if (arg == "--help") {
                std::cout << "Usage: " << argv[0] << " [--input log_file] [--output parquet_file] [--lines count] [--parser parser_type]" << std::endl;
                return 0;
            }
        }
        
        std::cout << "=== LogAI-CPP Performance Test ===" << std::endl;
        
        // Create a sample log file if the specified one doesn't exist
        if (!std::filesystem::exists(log_filepath)) {
            create_sample_log_file(log_filepath, num_lines);
        } else {
            std::cout << "Using existing log file: " << log_filepath << std::endl;
        }
        
        // Detect log format and configure the parser
        std::string format = detect_log_format(log_filepath);
        DataLoaderConfig config = configure_loader(log_filepath, format, parser_type);
        
        // Initialize file data loader
        FileDataLoader loader(config);
        
        // STEP 1: Load and parse the log file
        std::cout << "\nStep 1: Loading and parsing log file..." << std::endl;
        auto start_time = high_resolution_clock::now();
        
        std::vector<LogRecordObject> records = loader.load_data();
        
        auto parse_end_time = high_resolution_clock::now();
        print_timing("Log parsing", parse_end_time - start_time);
        std::cout << "Parsed " << records.size() << " log records" << std::endl;
        
        // STEP 2: Convert to Arrow Table and export to Parquet
        std::cout << "\nStep 2: Converting to Arrow Table and exporting to Parquet..." << std::endl;
        
        auto convert_start = high_resolution_clock::now();
        
        // Convert logs to Arrow table
        std::shared_ptr<arrow::Table> table = loader.log_to_dataframe(log_filepath, config.log_type);
        
        auto convert_end = high_resolution_clock::now();
        print_timing("DataFrame conversion", convert_end - convert_start);
        
        // STEP 3: Write to Parquet file
        std::cout << "\nStep 3: Writing to Parquet file..." << std::endl;
        auto parquet_start = high_resolution_clock::now();
        
        auto status = loader.write_to_parquet(table, parquet_filepath);
        
        auto parquet_end = high_resolution_clock::now();
        print_timing("Parquet export", parquet_end - parquet_start);
        
        if (status.ok()) {
            std::cout << "Successfully wrote data to " << parquet_filepath << std::endl;
        } else {
            std::cerr << "Failed to write Parquet file: " << status.ToString() << std::endl;
            return 1;
        }
        
        // STEP 4: Print total time
        auto total_end = high_resolution_clock::now();
        print_timing("\nTotal operation", total_end - start_time);
        
        std::cout << "\nPerformance test completed successfully!" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 