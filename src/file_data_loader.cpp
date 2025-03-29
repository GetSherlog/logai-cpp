/**
 * Copyright (c) 2023 LogAI Team
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * For full license text, see the LICENSE file in the repo root or https://opensource.org/licenses/BSD-3-Clause
 */

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include "file_data_loader.h"
#include "csv_parser.h"
#include "json_parser.h"
#include "regex_parser.h"
#include "drain_parser.h"
#include "simd_scanner.h"
#include <duckdb.hpp>
#include "preprocessor.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <fcntl.h>      // For open, O_RDONLY
#include <sys/mman.h>   // For mmap, munmap, PROT_READ, MAP_PRIVATE, MAP_FAILED
#include <sys/stat.h>   // For stat
#include <unistd.h>     // For close
#include <regex>
#include <zlib.h>
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <folly/String.h>
#include <folly/FBVector.h>

// Define namespace aliases to avoid conflicts
namespace fs = std::filesystem;

namespace logai {

// Constants
constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks for efficient file reading
constexpr size_t MAX_LINE_LENGTH = 1024 * 1024; // 1MB max line length
constexpr char LOGLINE_NAME[] = "log_message";
constexpr char SPAN_ID[] = "span_id";
constexpr char LOG_TIMESTAMPS[] = "timestamp";
constexpr char LABELS[] = "labels";

FileDataLoader::FileDataLoader(const std::string& filepath, const FileDataLoaderConfig& config)
    : filepath_(filepath), config_(config) {
    initInputStream();
    initParser();
}

void FileDataLoader::initInputStream() {
    if (config_.decompress || isCompressedFile()) {
        input_stream_ = openCompressedFile();
    } else {
        input_stream_ = std::make_unique<std::ifstream>(filepath_, std::ios::binary);
    }

    if (!input_stream_ || !input_stream_->good()) {
        throw std::runtime_error("Failed to open file: " + filepath_);
    }

    validateEncoding();
}

void FileDataLoader::initParser() {
    parser_ = LogParserFactory::create(config_.format);
}

void FileDataLoader::setFormat(const std::string& format) {
    config_.format = format;
    initParser();
}

std::unique_ptr<std::istream> FileDataLoader::openCompressedFile() {
    namespace bio = boost::iostreams;
    
    auto file = std::make_unique<std::ifstream>(filepath_, std::ios::binary);
    if (!file || !file->good()) {
        throw std::runtime_error("Failed to open compressed file: " + filepath_);
    }

    auto ext = getFileExtension();
    auto buffer = std::make_unique<bio::filtering_streambuf<bio::input>>();

    if (ext == "gz" || ext == "gzip") {
        buffer->push(bio::gzip_decompressor());
    } else if (ext == "bz2") {
        buffer->push(bio::bzip2_decompressor());
    } else if (ext == "z") {
        buffer->push(bio::zlib_decompressor());
    } else {
        throw std::runtime_error("Unsupported compression format: " + ext);
    }

    buffer->push(*file);
    return std::make_unique<std::istream>(buffer.release());
}

void FileDataLoader::loadData(std::vector<LogParser::LogEntry>& entries) {
    entries.clear();
    std::string line;

    // Skip header if needed
    if (config_.has_header) {
        std::getline(*input_stream_, line);
    }

    while (input_stream_->good()) {
        if (config_.logical_lines) {
            auto logical_line = readLogicalLine();
            if (!logical_line.empty()) {
                if (parser_->validate(logical_line)) {
                    entries.push_back(parser_->parse(logical_line));
                }
            }
        } else {
            if (std::getline(*input_stream_, line)) {
                boost::trim(line);
                if (!line.empty() && parser_->validate(line)) {
                    entries.push_back(parser_->parse(line));
                }
            }
        }
    }
}

void FileDataLoader::streamData(const std::function<bool(const LogParser::LogEntry&)>& callback) {
    std::string line;

    // Skip header if needed
    if (config_.has_header) {
        std::getline(*input_stream_, line);
    }

    while (input_stream_->good()) {
        if (config_.logical_lines) {
            auto logical_line = readLogicalLine();
            if (!logical_line.empty() && parser_->validate(logical_line)) {
                if (!callback(parser_->parse(logical_line))) {
                    break;
                }
            }
        } else {
            if (std::getline(*input_stream_, line)) {
                boost::trim(line);
                if (!line.empty() && parser_->validate(line)) {
                    if (!callback(parser_->parse(line))) {
                        break;
                    }
                }
            }
        }
    }
}

void FileDataLoader::processInChunks(size_t chunk_size, 
    const std::function<void(const std::vector<LogParser::LogEntry>&)>& callback) {
    
    std::vector<LogParser::LogEntry> chunk;
    chunk.reserve(chunk_size);
    std::string line;

    // Skip header if needed
    if (config_.has_header) {
        std::getline(*input_stream_, line);
    }

    while (input_stream_->good()) {
        if (config_.logical_lines) {
            auto logical_line = readLogicalLine();
            if (!logical_line.empty() && parser_->validate(logical_line)) {
                chunk.push_back(parser_->parse(logical_line));
                if (chunk.size() >= chunk_size) {
                    callback(chunk);
                    chunk.clear();
                    chunk.reserve(chunk_size);
                }
            }
        } else {
            if (std::getline(*input_stream_, line)) {
                boost::trim(line);
                if (!line.empty() && parser_->validate(line)) {
                    chunk.push_back(parser_->parse(line));
                    if (chunk.size() >= chunk_size) {
                        callback(chunk);
                        chunk.clear();
                        chunk.reserve(chunk_size);
                    }
                }
            }
        }
    }

    // Process remaining entries
    if (!chunk.empty()) {
        callback(chunk);
    }
}

std::string FileDataLoader::readLogicalLine() {
    std::string current_line;
    std::string line;

    // Read first line
    if (!std::getline(*input_stream_, line)) {
        return "";
    }

    boost::trim(line);
    if (line.empty()) {
        return "";
    }

    current_line = line;

    // Keep reading while we see continuation markers
    while (input_stream_->good() && isLogicalLineContinuation(line)) {
        if (!std::getline(*input_stream_, line)) {
            break;
        }

        if (line.empty()) {
            break;
        }

        // Handle different continuation styles
        if (boost::starts_with(line, " ") || boost::starts_with(line, "\t")) {
            current_line = handleIndentationContinuation(line, current_line);
        } else if (boost::ends_with(current_line, "\\")) {
            current_line = handleBackslashContinuation(line, current_line);
        }
    }

    return current_line;
}

bool FileDataLoader::isLogicalLineContinuation(const std::string& line) {
    return boost::ends_with(line, "\\") || 
           (input_stream_->good() && 
            std::streampos(input_stream_->tellg()) < std::streampos(input_stream_->seekg(0, std::ios::end).tellg()) &&
            (boost::starts_with(line, " ") || boost::starts_with(line, "\t")));
}

std::string FileDataLoader::handleIndentationContinuation(const std::string& line, std::string& current_line) {
    std::string trimmed_line = line;
    boost::trim_left(trimmed_line);
    return current_line + " " + trimmed_line;
}

std::string FileDataLoader::handleBackslashContinuation(const std::string& line, std::string& current_line) {
    // Remove backslash and join with next line
    current_line.pop_back();  // Remove backslash
    boost::trim(current_line);
    return current_line + line;
}

bool FileDataLoader::isCompressedFile() const {
    auto ext = getFileExtension();
    return ext == "gz" || ext == "gzip" || ext == "bz2" || ext == "z";
}

std::string FileDataLoader::getFileExtension() const {
    auto pos = filepath_.find_last_of('.');
    if (pos != std::string::npos) {
        return filepath_.substr(pos + 1);
    }
    return "";
}

void FileDataLoader::validateEncoding() const {
    // TODO: Implement encoding validation and conversion if needed
    // For now, we only support UTF-8 and ASCII
    if (config_.encoding != "utf-8" && config_.encoding != "ascii") {
        throw std::runtime_error("Unsupported encoding: " + config_.encoding);
    }
}

std::vector<LogRecordObject> FileDataLoader::load_data() {
    std::string filepath = config_.file_path;
    
    // Check if file exists
    if (!fs::exists(filepath)) {
        throw std::runtime_error("File does not exist: " + filepath);
    }
    
    std::vector<LogRecordObject> results;
    running_ = true;
    std::atomic<size_t> total_batches_{0};
    
    // Create queues for the producer-consumer pattern
    ThreadSafeQueue<LogBatch> input_queue;
    ThreadSafeQueue<ProcessedBatch> output_queue;
    
    // Start the producer thread to read the file
    MemoryMappedFile file;
    std::thread producer([this, &file, &input_queue, &total_batches_]() {
        producer_thread(file, input_queue, total_batches_);
    });
    
    // Determine number of worker threads
    size_t num_threads = config_.num_threads > 0 ? 
                        config_.num_threads : 
                        std::thread::hardware_concurrency();
    
    // Start worker threads to process batches
    std::vector<std::thread> workers;
    for (size_t i = 0; i < num_threads; i++) {
        workers.push_back(std::thread([this, &input_queue, &output_queue]() {
            worker_thread(input_queue, output_queue);
        }));
    }
    
    // Start consumer thread to collect results
    std::thread consumer([this, num_threads, &output_queue, &results, &total_batches_]() {
        consumer_thread(num_threads, output_queue, results, total_batches_);
    });
    
    // Wait for all threads to complete
    producer.join();
    for (auto& worker : workers) {
        worker.join();
    }
    
    // Signal that no more processed batches will be produced
    output_queue.done();
    consumer.join();
    
    running_ = false;
    return results;
}

std::unique_ptr<LogParser> FileDataLoader::create_parser() {
    if (config_.log_type == "csv") {
        return std::make_unique<CsvParser>(config_);
    } else if (config_.log_type == "json") {
        return std::make_unique<JsonParser>(config_);
    } else if (config_.log_type == "drain") {
        // Use the high-performance DRAIN parser for log pattern parsing
        return std::make_unique<DrainParser>(config_);
    } else {
        // Default to regex parser for custom log formats
        return std::make_unique<RegexParser>(config_, config_.log_pattern);
    }
}

void FileDataLoader::worker_thread(ThreadSafeQueue<LogBatch>& input_queue, 
                                 ThreadSafeQueue<ProcessedBatch>& output_queue) {
    try {
        // Create parser once per thread
        auto parser = create_parser();
        if (!parser) {
            throw std::runtime_error("Failed to create parser in worker thread");
        }
        
        std::cout << "Worker thread started" << std::endl;
        
        while (true) {
            LogBatch batch;
            if (!input_queue.wait_and_pop(batch)) {
                break; // Queue is done and empty
            }
            
            // Process the batch
            ProcessedBatch processed_batch;
            processed_batch.id = batch.id;
            processed_batch.records.reserve(batch.lines.size());
            
            size_t success_count = 0;
            size_t error_count = 0;
            
            for (const auto& line : batch.lines) {
                try {
                    if (!line.empty()) {
                        auto record = parser->parse_line(line);
                        processed_batch.records.push_back(std::move(record));
                        success_count++;
                    }
                } catch (const std::exception& e) {
                    error_count++;
                    if (error_count < 10) { // Limit error messages to avoid flooding logs
                        std::cerr << "Error parsing line: " << e.what() << std::endl;
                        if (line.length() < 200) { // Only print short lines to avoid flooding logs
                            std::cerr << "Line content: " << line << std::endl;
                        } else {
                            std::cerr << "Line too long to display (" << line.length() << " chars)" << std::endl;
                        }
                    } else if (error_count == 10) {
                        std::cerr << "Too many parsing errors, suppressing further messages..." << std::endl;
                    }
                }
            }
            
            if (batch.id % 10 == 0 || error_count > 0) {
                spdlog::info("Processed batch {}: {} successes, {} errors", 
                            batch.id, success_count, error_count);
            }
            
            // Push the processed batch to the output queue
            output_queue.push(std::move(processed_batch));
        }
        
        spdlog::info("Worker thread finished");
    } catch (const std::exception& e) {
        spdlog::error("Error in worker thread: {}", e.what());
    }
}

void FileDataLoader::consumer_thread(size_t num_threads, ThreadSafeQueue<ProcessedBatch>& output_queue, 
                                    std::vector<LogRecordObject>& results,
                                    std::atomic<size_t>& total_batches) {
    size_t processed_count = 0;
    size_t expected_count = total_batches.load();
    
    while (true) {
        ProcessedBatch batch;
        if (!output_queue.wait_and_pop(batch)) {
            break; // Queue is done and empty
        }
        
        // Add the processed records to the results
        results.insert(results.end(), 
                      std::make_move_iterator(batch.records.begin()), 
                      std::make_move_iterator(batch.records.end()));
        
        processed_count++;
        
        // If we've processed all expected batches, we're done
        if (processed_count >= expected_count && expected_count > 0) {
            break;
        }
    }
}

void FileDataLoader::producer_thread([[maybe_unused]] MemoryMappedFile& file, ThreadSafeQueue<LogBatch>& input_queue, 
                                    std::atomic<size_t>& total_batches) {
    try {
        if (config_.use_memory_mapping) {
            // Use a vector to collect lines in batches
            std::vector<std::string> batch_lines;
            batch_lines.reserve(current_batch_size_.load()); // Use adaptive batch size
            size_t batch_id = 0;
            size_t lines_processed = 0;
            
            read_file_memory_mapped(config_.file_path, [&](std::string_view line) {
                try {
                    // Check if the string_view is valid before creating a string from it
                    if (line.data() && line.size() > 0 && line.size() < MAX_LINE_LENGTH) {
                        // Create a safe copy of the string_view
                        std::string line_copy;
                        line_copy.reserve(line.size());
                        line_copy.assign(line.data(), line.size());
                        
                        // Add to current batch
                        batch_lines.push_back(std::move(line_copy));
                        lines_processed++;
                        
                        // If batch is full, push it to the queue
                        size_t current_batch_size = current_batch_size_.load();
                        if (batch_lines.size() >= current_batch_size) {
                            LogBatch batch{batch_id++, std::move(batch_lines)};
                            input_queue.push(std::move(batch));
                            
                            // Reset batch_lines for next batch
                            batch_lines = std::vector<std::string>();
                            batch_lines.reserve(current_batch_size);
                            
                            // Update total batches
                            total_batches.store(batch_id);
                            
                            // Adjust batch size based on queue size and memory usage
                            adjust_batch_size(input_queue);
                            
                            // If memory pressure is high, pause briefly to let consumers catch up
                            if (memory_pressure_.load()) {
                                size_t queue_size = input_queue.size();
                                if (queue_size > queue_high_watermark_.load()) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                }
                            }
                        }
                        
                        // Report progress periodically
                        if (lines_processed % 10000 == 0) {
                            spdlog::info("Processed {} lines", lines_processed);
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error creating batch: {}", e.what());
                }
            });
            
            // Push any remaining lines
            if (!batch_lines.empty()) {
                LogBatch batch{batch_id++, std::move(batch_lines)};
                input_queue.push(std::move(batch));
                total_batches.store(batch_id);
            }
        } else {
            // Use a vector to collect lines in batches
            std::vector<std::string> batch_lines;
            batch_lines.reserve(current_batch_size_.load()); // Use adaptive batch size
            size_t batch_id = 0;
            size_t lines_processed = 0;
            
            read_file_by_chunks(config_.file_path, [&](const std::string& line) {
                try {
                    // Add to current batch
                    batch_lines.push_back(line);
                    lines_processed++;
                    
                    // If batch is full, push it to the queue
                    size_t current_batch_size = current_batch_size_.load();
                    if (batch_lines.size() >= current_batch_size) {
                        LogBatch batch{batch_id++, std::move(batch_lines)};
                        input_queue.push(std::move(batch));
                        
                        // Reset batch_lines for next batch
                        batch_lines = std::vector<std::string>();
                        batch_lines.reserve(current_batch_size);
                        
                        // Update total batches
                        total_batches.store(batch_id);
                        
                        // Adjust batch size based on queue size and memory usage
                        adjust_batch_size(input_queue);
                        
                        // If memory pressure is high, pause briefly to let consumers catch up
                        if (memory_pressure_.load()) {
                            size_t queue_size = input_queue.size();
                            if (queue_size > queue_high_watermark_.load()) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            }
                        }
                    }
                    
                    // Report progress periodically
                    if (lines_processed % 10000 == 0) {
                        spdlog::info("Processed {} lines", lines_processed);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error creating batch: {}", e.what());
                }
            });
            
            // Push any remaining lines
            if (!batch_lines.empty()) {
                LogBatch batch{batch_id++, std::move(batch_lines)};
                input_queue.push(std::move(batch));
                total_batches.store(batch_id);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error in producer thread: {}", e.what());
    }
    
    // Signal that no more batches will be produced
    input_queue.done();
}

void FileDataLoader::read_file_by_chunks(const std::string& filepath, 
                                       const std::function<void(const std::string&)>& callback) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            callback(line);
        }
    }
}

void FileDataLoader::read_file_memory_mapped(const std::string& file_path, 
                                           std::function<void(std::string_view)> line_processor) {
    try {
        // Open the file
        int fd = open(file_path.c_str(), O_RDONLY);
        if (fd == -1) {
            throw std::runtime_error("Failed to open file: " + file_path + ", error: " + strerror(errno));
        }

        // Get file size
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            close(fd);
            throw std::runtime_error("Failed to get file size: " + file_path + ", error: " + strerror(errno));
        }

        // Map the file into memory
        void* mapped = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("Failed to map file: " + file_path + ", error: " + strerror(errno));
        }

        // Process the file line by line
        const char* data = static_cast<const char*>(mapped);
        const char* end = data + sb.st_size;
        const char* line_start = data;

        spdlog::info("Processing memory mapped file of size: {} bytes", sb.st_size);

        // Process each line
        size_t line_count = 0;
        while (line_start < end) {
            // Find the end of the current line
            const char* line_end = line_start;
            while (line_end < end && *line_end != '\n') {
                ++line_end;
            }

            // Create a string_view for the current line
            size_t line_length = line_end - line_start;
            if (line_length > 0 && line_length < MAX_LINE_LENGTH) {
                try {
                    std::string_view line(line_start, line_length);
                    line_processor(line);
                    line_count++;
                    
                    if (line_count % 10000 == 0) {
                        spdlog::info("Processed {} lines", line_count);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error processing line: {}", e.what());
                }
            } else if (line_length >= MAX_LINE_LENGTH) {
                spdlog::error("Skipping line {} (length: {}): Line too long", line_count, line_length);
            }

            // Move to the next line
            line_start = (line_end < end) ? line_end + 1 : end;
        }

        spdlog::info("Finished processing {} lines", line_count);

        // Unmap and close the file
        if (munmap(mapped, sb.st_size) == -1) {
            spdlog::error("Warning: Failed to unmap file: {}", file_path);
        }
        close(fd);
    } catch (const std::exception& e) {
        spdlog::error("Error in read_file_memory_mapped: {}", e.what());
        throw;
    }
}

std::vector<LogRecordObject> FileDataLoader::read_logs(const std::string& filepath) {
    std::vector<LogRecordObject> records;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    
    auto parser = create_parser();
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        try {
            records.push_back(parser->parse_line(line));
        } catch (const std::exception& e) {
            spdlog::error("Error parsing line: {}", e.what());
        }
    }
    
    return records;
}

std::vector<LogRecordObject> FileDataLoader::read_csv(const std::string& filepath) {
    return read_logs(filepath);
}

std::vector<LogRecordObject> FileDataLoader::read_tsv(const std::string& filepath) {
    return read_logs(filepath);
}

std::vector<LogRecordObject> FileDataLoader::read_json(const std::string& filepath) {
    return read_logs(filepath);
}

// DuckDB integration has been removed from this implementation
// The following methods have been removed:
// - log_to_duckdb_table
// - filter_duckdb_table
// - export_to_csv
// - extract_attributes (DuckDB version)
// - process_large_file (DuckDB version)
// - create_table_from_records

std::unordered_map<std::string, std::vector<std::string>> FileDataLoader::extract_attributes(
    const std::vector<std::string>& log_lines,
    const std::unordered_map<std::string, std::string>& patterns) {
    
    std::unordered_map<std::string, std::vector<std::string>> result;
    
    try {
        // Initialize result map with empty vectors for all pattern keys
        for (const auto& [name, _] : patterns) {
            result[name] = std::vector<std::string>();
            result[name].reserve(log_lines.size());
        }
        
        // Extract attributes using regex for each line
        for (const auto& line : log_lines) {
            for (const auto& [name, pattern_str] : patterns) {
                std::regex pattern(pattern_str);
                std::smatch match;
                
                if (std::regex_search(line, match, pattern) && match.size() > 1) {
                    // Use the first capturing group
                    result[name].push_back(match[1].str());
                } else {
                    // No match
                    result[name].push_back("");
                }
            }
        }
        
        return result;
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to extract attributes: {}", e.what());
        return result;
    }
}

// Process a single batch of log lines
void FileDataLoader::process_batch(const LogBatch& batch, ProcessedBatch& result) {
    result.id = batch.id;
    result.records.reserve(batch.lines.size());
    
    // Apply preprocessing if enabled
    std::vector<std::string> preprocessed_lines;
    if (config_.enable_preprocessing) {
        preprocessed_lines = preprocess_logs(batch.lines);
    } else {
        preprocessed_lines = batch.lines;
    }
    
    // Parse each line
    for (const auto& line : preprocessed_lines) {
        try {
            if (parser_->validate(line)) {
                LogParser::LogEntry entry = parser_->parse(line);
                result.records.push_back(entry.to_record_object());
                processed_lines_++;
            } else {
                failed_lines_++;
            }
        } catch (const std::exception& e) {
            // Log error and continue
            spdlog::warn("Failed to parse line: {}", e.what());
            failed_lines_++;
        }
    }
}

// Initialize preprocessor if needed
void FileDataLoader::init_preprocessor() {
    if (config_.enable_preprocessing && !preprocessor_) {
        PreprocessorConfig preprocessor_config;
        // Set config properties if available
        preprocessor_ = std::make_unique<Preprocessor>(preprocessor_config);
    }
}

// Add new methods for parsing logs

std::vector<LogRecordObject> FileDataLoader::parse_log_file(const std::string& filepath, const std::string& format) {
    try {
        // Set format for parser
        if (!format.empty()) {
            setFormat(format);
        }
        
        // Load and parse the log data
        return read_logs(filepath);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to parse log file: {}", e.what());
        return std::vector<LogRecordObject>();
    }
}

bool FileDataLoader::process_large_file_with_callback(
    const std::string& input_file,
    const std::string& parser_type,
    size_t chunk_size,
    const std::function<void(const std::vector<LogRecordObject>&)>& callback,
    size_t memory_limit_mb) {
    
    try {
        // Set format for parser
        if (!parser_type.empty()) {
            setFormat(parser_type);
        }
        
        // Check if file exists
        if (!std::filesystem::exists(input_file)) {
            spdlog::error("Input file does not exist: {}", input_file);
            return false;
        }
        
        // Get file size
        auto file_size = std::filesystem::file_size(input_file);
        
        // For very small files, just read everything at once
        if (file_size < chunk_size * 100 && file_size < memory_limit_mb * 1024 * 1024 / 10) {
            auto records = read_logs(input_file);
            if (!records.empty()) {
                callback(records);
                return true;
            }
            return false;
        }
        
        // For large files, process in chunks
        MemoryMappedFile mmapped_file(input_file);
        if (!mmapped_file.isOpen()) {
            spdlog::error("Failed to memory map file: {}", input_file);
            return false;
        }
        
        // Process file in chunks using producer-consumer pattern
        const size_t line_estimate = chunk_size;
        std::atomic<size_t> total_batches{0};
        ThreadSafeQueue<LogBatch> input_queue;
        ThreadSafeQueue<ProcessedBatch> output_queue;
        
        // Start producer thread to read file
        std::thread producer([this, &mmapped_file, &input_queue, &total_batches, line_estimate]() {
            producer_thread(mmapped_file, input_queue, total_batches);
        });
        
        // Start worker threads to process batches
        const size_t num_workers = std::thread::hardware_concurrency();
        std::vector<std::thread> workers;
        for (size_t i = 0; i < num_workers; i++) {
            workers.emplace_back([this, &input_queue, &output_queue]() {
                worker_thread(input_queue, output_queue);
            });
        }
        
        // Process output batches as they become available
        std::thread consumer([this, &output_queue, &callback, num_workers, &total_batches]() {
            // Process batches in order
            std::map<size_t, ProcessedBatch> pending_batches;
            size_t next_batch_id = 0;
            
            while (true) {
                // Check if we're done
                if (output_queue.empty() && total_batches.load() > 0 && 
                    next_batch_id >= total_batches.load()) {
                    break;
                }
                
                // Get next batch
                ProcessedBatch batch;
                if (!output_queue.try_pop(batch)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                // Store batch for later if it's not the next one
                if (batch.id != next_batch_id) {
                    pending_batches[batch.id] = std::move(batch);
                    continue;
                }
                
                // Process this batch and any pending batches that are now ready
                do {
                    // Process the current batch
                    callback(batch.records);
                    next_batch_id++;
                    
                    // Check if the next batch is in pending
                    auto it = pending_batches.find(next_batch_id);
                    if (it == pending_batches.end()) {
                        break;
                    }
                    
                    // Process the pending batch
                    batch = std::move(it->second);
                    pending_batches.erase(it);
                } while (true);
            }
        });
        
        // Wait for all threads to finish
        producer.join();
        for (auto& worker : workers) {
            worker.join();
        }
        consumer.join();
        
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to process large file: {}", e.what());
        return false;
    }
}

} // namespace logai 