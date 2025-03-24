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
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/compute/api.h>
#include <arrow/compute/api_scalar.h>
#include <arrow/compute/api_vector.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/exec.h>
#include <arrow/compute/expression.h>
#include <parquet/arrow/writer.h>
#include <parquet/exception.h>
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

// Define namespace aliases to avoid conflicts
namespace fs = std::filesystem;

// Bring std types into global scope
using std::string;
using std::vector;
using std::unique_ptr;
using std::make_unique;
using std::thread;
using std::function;
using std::ifstream;
using std::ofstream;
using std::string_view;
using std::cerr;
using std::endl;
using std::runtime_error;
using std::move;
using std::atomic;

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

void FileDataLoader::producer_thread(MemoryMappedFile& file, ThreadSafeQueue<LogBatch>& input_queue, 
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

std::shared_ptr<arrow::Table> FileDataLoader::log_to_dataframe(const std::string& filepath, const std::string& log_format) {
    // Load log records
    std::vector<LogRecordObject> log_records;
    
    if (log_format == "CSV") {
        log_records = read_csv(filepath);
    } else if (log_format == "JSON") {
        log_records = read_json(filepath);
    } else {
        log_records = read_logs(filepath);
    }
    
    // Create Arrow builders
    arrow::StringBuilder body_builder;
    arrow::StringBuilder severity_builder;
    arrow::Int64Builder timestamp_builder;
    
    // Create builders for attributes (dynamically determine them from the data)
    std::unordered_map<std::string, std::shared_ptr<arrow::StringBuilder>> attribute_builders;
    std::unordered_set<std::string> attribute_keys;
    
    // First pass: collect all attribute keys
    for (const auto& record : log_records) {
        for (const auto& [key, value] : record.attributes) {
            attribute_keys.insert(key);
        }
    }
    
    // Initialize attribute builders
    for (const auto& key : attribute_keys) {
        attribute_builders[key] = std::make_shared<arrow::StringBuilder>();
    }
    
    // Second pass: populate the arrays
    for (const auto& record : log_records) {
        // Add body
        auto status = body_builder.Append(record.body);
        if (!status.ok()) {
            throw std::runtime_error("Failed to append body: " + status.ToString());
        }
        
        // Add severity
        if (record.severity) {
            status = severity_builder.Append(*record.severity);
            if (!status.ok()) {
                throw std::runtime_error("Failed to append severity: " + status.ToString());
            }
        } else {
            status = severity_builder.AppendNull();
            if (!status.ok()) {
                throw std::runtime_error("Failed to append null severity: " + status.ToString());
            }
        }
        
        // Add timestamp
        if (record.timestamp) {
            auto timestamp_value = std::chrono::duration_cast<std::chrono::milliseconds>(
                record.timestamp->time_since_epoch()).count();
            status = timestamp_builder.Append(timestamp_value);
            if (!status.ok()) {
                throw std::runtime_error("Failed to append timestamp: " + status.ToString());
            }
        } else {
            status = timestamp_builder.AppendNull();
            if (!status.ok()) {
                throw std::runtime_error("Failed to append null timestamp: " + status.ToString());
            }
        }
        
        // Add attributes
        for (const auto& key : attribute_keys) {
            auto builder = attribute_builders[key];
            auto it = record.attributes.find(key);
            
            if (it != record.attributes.end()) {
                status = builder->Append(it->second);
                if (!status.ok()) {
                    throw std::runtime_error("Failed to append attribute: " + status.ToString());
                }
            } else {
                status = builder->AppendNull();
                if (!status.ok()) {
                    throw std::runtime_error("Failed to append null attribute: " + status.ToString());
                }
            }
        }
    }
    
    // Finalize arrays
    std::shared_ptr<arrow::Array> body_array;
    auto result = body_builder.Finish();
    if (!result.ok()) {
        throw std::runtime_error("Failed to finish body array: " + result.status().ToString());
    }
    body_array = result.ValueOrDie();
    
    std::shared_ptr<arrow::Array> severity_array;
    result = severity_builder.Finish();
    if (!result.ok()) {
        throw std::runtime_error("Failed to finish severity array: " + result.status().ToString());
    }
    severity_array = result.ValueOrDie();
    
    std::shared_ptr<arrow::Array> timestamp_array;
    result = timestamp_builder.Finish();
    if (!result.ok()) {
        throw std::runtime_error("Failed to finish timestamp array: " + result.status().ToString());
    }
    timestamp_array = result.ValueOrDie();
    
    // Create field vector and arrays vector
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::Array>> arrays;
    
    // Add standard fields
    fields.push_back(arrow::field("body", arrow::utf8()));
    arrays.push_back(body_array);
    
    fields.push_back(arrow::field("severity", arrow::utf8()));
    arrays.push_back(severity_array);
    
    fields.push_back(arrow::field("timestamp", arrow::int64()));
    arrays.push_back(timestamp_array);
    
    // Add attribute fields
    for (const auto& key : attribute_keys) {
        fields.push_back(arrow::field(key, arrow::utf8()));
        std::shared_ptr<arrow::Array> array;
        result = attribute_builders[key]->Finish();
        if (!result.ok()) {
            throw std::runtime_error("Failed to finish attribute array: " + result.status().ToString());
        }
        array = result.ValueOrDie();
        arrays.push_back(array);
    }
    
    // Create schema
    auto schema = std::make_shared<arrow::Schema>(fields);
    
    // Create table
    return arrow::Table::Make(schema, arrays);
}

std::shared_ptr<arrow::Table> FileDataLoader::filter_dataframe(std::shared_ptr<arrow::Table> table, 
                                                              const std::string& column, 
                                                              const std::string& op, 
                                                              const std::string& value) {
    try {
        // For now, just return the original table
        // This is a simplified implementation to avoid Arrow API compatibility issues
        std::cout << "Warning: filter_dataframe is not fully implemented in this version." << std::endl;
        return table;
    } catch (const std::exception& e) {
        throw std::runtime_error("Error filtering dataframe: " + std::string(e.what()));
    }
}

arrow::Status FileDataLoader::write_to_parquet(std::shared_ptr<arrow::Table> table, 
                                             const std::string& output_filepath) {
    try {
        // Create output file
        std::shared_ptr<arrow::io::FileOutputStream> outfile;
        ARROW_ASSIGN_OR_RAISE(outfile, arrow::io::FileOutputStream::Open(output_filepath));
        
        // Set up Parquet writer properties with default settings
        std::shared_ptr<parquet::WriterProperties> props = parquet::default_writer_properties();
            
        // Set up Arrow-specific Parquet writer properties with default settings
        std::shared_ptr<parquet::ArrowWriterProperties> arrow_props = parquet::default_arrow_writer_properties();
            
        // Write table to Parquet file
        PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), 
                                                     outfile, /*chunk_size=*/65536,
                                                     props, arrow_props));
        
        return arrow::Status::OK();
    } catch (const std::exception& e) {
        return arrow::Status::IOError("Error writing Parquet file: ", e.what());
    }
}

// Add memory monitoring and adaptive batch size implementations
size_t FileDataLoader::get_current_memory_usage() const {
    // Implementation for Linux-based systems
    #ifdef __linux__
    FILE* file = fopen("/proc/self/status", "r");
    if (file == nullptr) {
        return 0;
    }
    
    size_t memory = 0;
    char line[128];
    
    while (fgets(line, 128, file) != nullptr) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            // Extract the value (in kB)
            long value = 0;
            sscanf(line, "VmRSS: %ld", &value);
            memory = static_cast<size_t>(value * 1024); // Convert to bytes
            break;
        }
    }
    
    fclose(file);
    return memory;
    #elif defined(__APPLE__)
    // macOS implementation could use mach calls, but simplified version returns estimated usage
    return processed_lines_.load() * 1024; // Rough estimate based on lines processed
    #else
    // For other platforms, return a conservative estimate
    return processed_lines_.load() * 1024;
    #endif
}

bool FileDataLoader::detect_memory_pressure() const {
    // Check system memory pressure
    const size_t memory_usage = get_current_memory_usage();
    const size_t memory_threshold = 3UL * 1024 * 1024 * 1024; // 3GB threshold
    
    if (memory_usage > memory_threshold) {
        return true;
    }
    
    // Check if queue is growing too large
    if (batch_queue_.size() > queue_high_watermark_.load()) {
        return true;
    }
    
    return false;
}

void FileDataLoader::adjust_batch_size(ThreadSafeQueue<LogBatch>& queue) {
    size_t current_size = current_batch_size_.load();
    
    // Check for memory pressure
    if (detect_memory_pressure()) {
        // Under memory pressure, reduce batch size
        size_t new_size = current_size / 2;
        if (new_size < min_batch_size_.load()) {
            new_size = min_batch_size_.load();
        }
        
        if (new_size != current_size) {
            current_batch_size_.store(new_size);
            std::cout << "Memory pressure detected: Reduced batch size to " << new_size << std::endl;
            
            // Set memory pressure flag
            memory_pressure_.store(true);
        }
    } else if (queue.size() < queue_low_watermark_.load() && !memory_pressure_.load()) {
        // Queue is small and no memory pressure, increase batch size
        size_t new_size = current_size * 2;
        if (new_size > max_batch_size_.load()) {
            new_size = max_batch_size_.load();
        }
        
        if (new_size != current_size) {
            current_batch_size_.store(new_size);
            std::cout << "Increased batch size to " << new_size << std::endl;
        }
    }
}

void FileDataLoader::process_in_chunks(const std::string& filepath, size_t chunk_size, const std::string& output_dir) {
    // Get file size
    std::filesystem::path path(filepath);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File does not exist: " + filepath);
    }
    
    size_t file_size = std::filesystem::file_size(path);
    size_t num_chunks = (file_size + chunk_size - 1) / chunk_size; // Ceiling division
    
    std::cout << "Processing file in " << num_chunks << " chunks" << std::endl;
    
    // Create output directory if it doesn't exist
    std::filesystem::path output_path(output_dir);
    if (!std::filesystem::exists(output_path)) {
        std::filesystem::create_directories(output_path);
    }
    
    // Process each chunk
    for (size_t i = 0; i < num_chunks; i++) {
        std::cout << "Processing chunk " << (i + 1) << " of " << num_chunks << std::endl;
        
        // Calculate chunk bounds
        size_t start_offset = i * chunk_size;
        size_t end_offset = std::min((i + 1) * chunk_size, file_size);
        
        // Open file for reading chunk
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }
        
        // Seek to start of chunk
        file.seekg(start_offset);
        
        // Read chunk data
        std::vector<char> buffer(end_offset - start_offset);
        file.read(buffer.data(), buffer.size());
        
        // Create temporary file for this chunk
        std::string temp_filename = output_dir + "/chunk_" + std::to_string(i) + ".log";
        std::ofstream temp_file(temp_filename);
        
        if (!temp_file.is_open()) {
            throw std::runtime_error("Failed to create temporary file: " + temp_filename);
        }
        
        // Find line boundaries
        size_t pos = 0;
        // Skip partial line at beginning (except for first chunk)
        if (i > 0) {
            while (pos < buffer.size() && buffer[pos] != '\n') {
                pos++;
            }
            if (pos < buffer.size()) {
                pos++; // Skip the newline
            }
        }
        
        // Process lines in this chunk
        size_t line_start = pos;
        while (pos < buffer.size()) {
            if (buffer[pos] == '\n') {
                // Found end of line, write it to temp file
                temp_file.write(&buffer[line_start], pos - line_start + 1);
                line_start = pos + 1;
            }
            pos++;
        }
        
        // Handle the last line in the chunk
        if (line_start < buffer.size()) {
            temp_file.write(&buffer[line_start], buffer.size() - line_start);
        }
        
        temp_file.close();
        
        // Process this chunk with normal data loading
        DataLoaderConfig chunk_config = config_;
        chunk_config.file_path = temp_filename;
        
        // Create a new loader for this chunk with the modified config
        FileDataLoader chunk_loader(chunk_config);
        auto records = chunk_loader.load_data();
        
        // Create a DataFrame for this chunk
        auto table = chunk_loader.log_to_dataframe(temp_filename, config_.log_type);
        
        // Write the chunk to a Parquet file
        std::string output_filename = output_dir + "/chunk_" + std::to_string(i) + ".parquet";
        chunk_loader.write_to_parquet(table, output_filename);
        
        // Clean up temporary file
        std::filesystem::remove(temp_filename);
        
        std::cout << "Completed chunk " << (i + 1) << " of " << num_chunks << std::endl;
    }
    
    std::cout << "All chunks processed successfully" << std::endl;
}

/**
 * @brief Process a large file in memory-adaptive chunks 
 * @param output_file The output file path
 * @param memory_limit The memory limit in bytes
 * @return std::shared_ptr<arrow::Table> The final Arrow table result
 */
std::shared_ptr<arrow::Table> FileDataLoader::process_large_file(
    const std::string& input_file,
    const std::string& parser_type,
    size_t memory_limit_mb,
    size_t chunk_size,
    bool force_chunking) {
    
    // Check if file exists
    if (!std::filesystem::exists(input_file)) {
        throw std::runtime_error("Input file does not exist: " + input_file);
    }
    
    // Get file size in bytes for accurate line estimation
    size_t file_size_bytes = std::filesystem::file_size(input_file);
    size_t file_size_mb = file_size_bytes / (1024 * 1024);
    std::cout << "File size: " << file_size_mb << " MB" << std::endl;
    
    // If file is small enough and not forced to chunk, process directly
    if (file_size_mb < memory_limit_mb / 2 && !force_chunking) {
        std::cout << "File is small enough to process directly." << std::endl;
        
        // Configure for this specific file
        DataLoaderConfig process_config = config_;
        process_config.file_path = input_file;
        process_config.log_type = parser_type;
        process_config.num_threads = std::min(8, (int)std::thread::hardware_concurrency());
        
        // Use the file data loader to load the data
        FileDataLoader direct_loader(process_config);
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<LogRecordObject> records = direct_loader.load_data();
        
        // Create an Arrow table from the records
        std::shared_ptr<arrow::Table> table = direct_loader.log_to_dataframe(input_file, parser_type);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "Processed " << records.size() << " records in " 
                  << duration.count() << " ms" << std::endl;
        
        return table;
    }
    
    // For larger files, process in chunks
    std::cout << "File is too large to process directly. Processing in chunks..." << std::endl;
    
    // Create temporary directory for chunk processing
    std::string temp_dir = std::filesystem::temp_directory_path().string() + "/logai_chunks_" + 
                         std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(temp_dir);
    std::cout << "Created temporary directory: " << temp_dir << std::endl;
    
    // Estimate number of lines based on a sample
    std::ifstream sample_file(input_file);
    if (!sample_file) {
        throw std::runtime_error("Failed to open input file: " + input_file);
    }
    
    size_t sample_size = 1000;
    size_t sample_lines = 0;
    size_t total_sample_bytes = 0;
    std::string line;
    
    while (sample_lines < sample_size && std::getline(sample_file, line)) {
        total_sample_bytes += line.size() + 1; // +1 for newline
        sample_lines++;
    }
    
    // Estimate total lines
    double bytes_per_line = static_cast<double>(total_sample_bytes) / sample_lines;
    size_t estimated_total_lines = static_cast<size_t>(file_size_bytes / bytes_per_line);
    
    std::cout << "Estimated total lines: " << estimated_total_lines << std::endl;
    
    // Calculate number of chunks needed
    size_t lines_per_chunk = chunk_size;
    size_t num_chunks = (estimated_total_lines + lines_per_chunk - 1) / lines_per_chunk;
    
    std::cout << "Processing file in " << num_chunks << " chunks of " 
              << lines_per_chunk << " lines each" << std::endl;
    
    // Process file in chunks
    std::vector<std::shared_ptr<arrow::Table>> chunk_tables;
    
    std::ifstream input(input_file);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + input_file);
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
        std::cout << "Processing chunk " << (chunk_idx + 1) << " of " << num_chunks << std::endl;
        
        // Create chunk file
        std::string chunk_file = temp_dir + "/chunk_" + std::to_string(chunk_idx) + ".log";
        std::ofstream chunk_out(chunk_file);
        
        if (!chunk_out) {
            throw std::runtime_error("Failed to create chunk file: " + chunk_file);
        }
        
        // Read lines for this chunk
        size_t lines_read = 0;
        line.clear();
        
        while (lines_read < lines_per_chunk && std::getline(input, line)) {
            chunk_out << line << std::endl;
            lines_read++;
        }
        
        chunk_out.close();
        
        if (lines_read == 0) {
            // No more lines to read
            break;
        }
        
        // Process this chunk
        DataLoaderConfig chunk_config = config_;
        chunk_config.file_path = chunk_file;
        chunk_config.log_type = parser_type;
        chunk_config.num_threads = std::min(4, (int)std::thread::hardware_concurrency());
        
        FileDataLoader chunk_loader(chunk_config);
        std::shared_ptr<arrow::Table> chunk_table = chunk_loader.log_to_dataframe(chunk_file, parser_type);
        
        if (chunk_table && chunk_table->num_rows() > 0) {
            chunk_tables.push_back(chunk_table);
            std::cout << "  Chunk " << (chunk_idx + 1) << " processed with " 
                      << chunk_table->num_rows() << " records" << std::endl;
        }
        
        // Clean up the chunk file
        std::filesystem::remove(chunk_file);
    }
    
    // Combine all chunk tables
    std::shared_ptr<arrow::Table> combined_table;
    
    if (chunk_tables.empty()) {
        throw std::runtime_error("No data processed from file");
    } else if (chunk_tables.size() == 1) {
        combined_table = chunk_tables[0];
    } else {
        // Unify schemas before concatenation
        std::cout << "Unifying schemas across " << chunk_tables.size() << " chunks..." << std::endl;
        auto unified_tables = unify_table_schemas(chunk_tables);
        
        // Combine multiple tables with unified schemas
        arrow::Result<std::shared_ptr<arrow::Table>> result = arrow::ConcatenateTables(unified_tables);
        
        if (!result.ok()) {
            throw std::runtime_error("Failed to concatenate tables: " + result.status().ToString());
        }
        
        combined_table = result.ValueOrDie();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Processing complete. Total records: " << combined_table->num_rows() << std::endl;
    std::cout << "Total processing time: " << duration.count() << " ms" << std::endl;
    
    // Clean up temporary directory
    std::filesystem::remove_all(temp_dir);
    
    return combined_table;
}

/**
 * @brief Create a unified schema from multiple Arrow tables
 * @param tables The vector of Arrow tables to unify
 * @return A vector of tables with unified schemas ready for concatenation
 */
std::vector<std::shared_ptr<arrow::Table>> FileDataLoader::unify_table_schemas(
    const std::vector<std::shared_ptr<arrow::Table>>& tables) {
    
    if (tables.empty()) {
        return {};
    }
    
    if (tables.size() == 1) {
        return tables;
    }
    
    // Step 1: Collect all field names across all tables
    std::unordered_set<std::string> all_field_names;
    
    for (const auto& table : tables) {
        const auto& schema = table->schema();
        for (int i = 0; i < schema->num_fields(); ++i) {
            all_field_names.insert(schema->field(i)->name());
        }
    }
    
    // Step 2: Create a vector of field names in a consistent order
    std::vector<std::string> ordered_field_names(all_field_names.begin(), all_field_names.end());
    std::sort(ordered_field_names.begin(), ordered_field_names.end());
    
    // Ensure core fields come first
    auto body_it = std::find(ordered_field_names.begin(), ordered_field_names.end(), "body");
    if (body_it != ordered_field_names.end()) {
        ordered_field_names.erase(body_it);
        ordered_field_names.insert(ordered_field_names.begin(), "body");
    }
    
    auto severity_it = std::find(ordered_field_names.begin(), ordered_field_names.end(), "severity");
    if (severity_it != ordered_field_names.end()) {
        ordered_field_names.erase(severity_it);
        ordered_field_names.insert(ordered_field_names.begin() + 1, "severity");
    }
    
    auto timestamp_it = std::find(ordered_field_names.begin(), ordered_field_names.end(), "timestamp");
    if (timestamp_it != ordered_field_names.end()) {
        ordered_field_names.erase(timestamp_it);
        ordered_field_names.insert(ordered_field_names.begin() + 2, "timestamp");
    }
    
    // Step 3: Create a unified schema
    std::vector<std::shared_ptr<arrow::Field>> unified_fields;
    
    for (const auto& field_name : ordered_field_names) {
        // Find a non-null field with this name in the tables
        for (const auto& table : tables) {
            if (table->schema()->GetFieldIndex(field_name) >= 0) {
                unified_fields.push_back(table->schema()->GetFieldByName(field_name));
                break;
            }
        }
    }
    
    auto unified_schema = std::make_shared<arrow::Schema>(unified_fields);
    
    // Step 4: Project each table to the unified schema
    std::vector<std::shared_ptr<arrow::Table>> unified_tables;
    
    for (const auto& table : tables) {
        // Create projection indices
        std::vector<int> projection_indices;
        std::vector<std::string> missing_fields;
        
        for (const auto& field_name : ordered_field_names) {
            auto idx = table->schema()->GetFieldIndex(field_name);
            if (idx >= 0) {
                projection_indices.push_back(idx);
            } else {
                missing_fields.push_back(field_name);
            }
        }
        
        // Select columns based on projection indices
        std::vector<std::shared_ptr<arrow::ChunkedArray>> projected_columns;
        
        for (int idx : projection_indices) {
            projected_columns.push_back(table->column(idx));
        }
        
        // Add null columns for missing fields
        for (const auto& field_name : missing_fields) {
            // Get the field from the unified schema
            auto field = unified_schema->GetFieldByName(field_name);
            
            // Create a null array of the correct type
            std::shared_ptr<arrow::Array> null_array;
            if (field->type()->id() == arrow::Type::STRING) {
                arrow::StringBuilder builder;
                for (int64_t i = 0; i < table->num_rows(); ++i) {
                    auto status = builder.AppendNull();
                    if (!status.ok()) {
                        throw std::runtime_error("Failed to append null to string builder: " + status.ToString());
                    }
                }
                auto result = builder.Finish();
                if (!result.ok()) {
                    throw std::runtime_error("Failed to finish string builder: " + result.status().ToString());
                }
                null_array = result.ValueOrDie();
            } else if (field->type()->id() == arrow::Type::INT64) {
                arrow::Int64Builder builder;
                for (int64_t i = 0; i < table->num_rows(); ++i) {
                    auto status = builder.AppendNull();
                    if (!status.ok()) {
                        throw std::runtime_error("Failed to append null to int64 builder: " + status.ToString());
                    }
                }
                auto result = builder.Finish();
                if (!result.ok()) {
                    throw std::runtime_error("Failed to finish int64 builder: " + result.status().ToString());
                }
                null_array = result.ValueOrDie();
            } else {
                // Default to null array
                null_array = std::make_shared<arrow::NullArray>(table->num_rows());
            }
            
            projected_columns.push_back(std::make_shared<arrow::ChunkedArray>(std::vector<std::shared_ptr<arrow::Array>>{null_array}));
        }
        
        // Create a new table with projected columns
        auto projected_table = arrow::Table::Make(unified_schema, projected_columns);
        unified_tables.push_back(projected_table);
    }
    
    return unified_tables;
}

void FileDataLoader::init_preprocessor() {
    if (config_.enable_preprocessing && !preprocessor_) {
        PreprocessorConfig preprocessor_config(
            config_.custom_delimiters_regex,
            config_.custom_replace_list
        );
        preprocessor_ = std::make_unique<Preprocessor>(preprocessor_config);
    }
}

std::vector<std::string> FileDataLoader::preprocess_logs(const std::vector<std::string>& log_lines) {
    if (!config_.enable_preprocessing || log_lines.empty()) {
        return log_lines;  // Return original lines if preprocessing is disabled
    }
    
    // Initialize preprocessor if not already done
    init_preprocessor();
    
    // Apply preprocessing
    auto [cleaned_logs, extracted_terms] = preprocessor_->clean_log_batch(log_lines);
    
    // TODO: Store or process extracted terms if needed
    
    return cleaned_logs;
}

std::shared_ptr<arrow::Table> FileDataLoader::extract_attributes(
    const std::vector<std::string>& log_lines,
    const std::unordered_map<std::string, std::string>& patterns) {
    
    if (log_lines.empty() || patterns.empty()) {
        return nullptr;
    }
    
    // Create schema
    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.push_back(arrow::field("log_line", arrow::utf8()));
    
    for (const auto& [name, _] : patterns) {
        fields.push_back(arrow::field(name, arrow::utf8()));
    }
    
    auto schema = arrow::schema(fields);
    
    // Prepare builders for each column
    arrow::StringBuilder log_line_builder;
    std::unordered_map<std::string, std::unique_ptr<arrow::StringBuilder>> attribute_builders;
    
    for (const auto& [name, _] : patterns) {
        attribute_builders[name] = std::make_unique<arrow::StringBuilder>();
    }
    
    // Reserve capacity for all builders
    auto status = log_line_builder.Reserve(log_lines.size());
    if (!status.ok()) {
        return nullptr;
    }
    
    for (auto& [_, builder] : attribute_builders) {
        status = builder->Reserve(log_lines.size());
        if (!status.ok()) {
            return nullptr;
        }
    }
    
    // Process each log line
    for (const auto& line : log_lines) {
        // Add the log line to the first column
        status = log_line_builder.Append(line);
        if (!status.ok()) {
            return nullptr;
        }
        
        // Extract attributes using regex patterns
        for (const auto& [name, pattern] : patterns) {
            std::smatch match;
            std::string value;
            
            try {
                std::regex regex_pattern(pattern);
                if (std::regex_search(line, match, regex_pattern) && match.size() > 0) {
                    value = match.str(match.size() > 1 ? 1 : 0);  // Use first capture group if available
                }
            } catch (const std::regex_error& e) {
                std::cerr << "Invalid regex pattern: " << pattern << " - " << e.what() << std::endl;
            }
            
            status = attribute_builders[name]->Append(value);
            if (!status.ok()) {
                return nullptr;
            }
        }
    }
    
    // Finalize arrays
    std::shared_ptr<arrow::Array> log_line_array;
    status = log_line_builder.Finish(&log_line_array);
    if (!status.ok()) {
        return nullptr;
    }
    
    std::vector<std::shared_ptr<arrow::Array>> arrays;
    arrays.push_back(log_line_array);
    
    for (const auto& [name, _] : patterns) {
        std::shared_ptr<arrow::Array> array;
        status = attribute_builders[name]->Finish(&array);
        if (!status.ok()) {
            return nullptr;
        }
        arrays.push_back(array);
    }
    
    // Create table
    return arrow::Table::Make(schema, arrays);
}

// Modify the process_batch method to include preprocessing
ProcessedBatch FileDataLoader::process_batch(const LogBatch& batch, const std::string& log_format) {
    ProcessedBatch result;
    result.id = batch.id;
    result.records.reserve(batch.lines.size());
    
    // Apply preprocessing if enabled
    std::vector<std::string> preprocessed_lines;
    if (config_.enable_preprocessing) {
        preprocessed_lines = preprocess_logs(batch.lines);
    } else {
        preprocessed_lines = batch.lines;
    }
    
    // Create parser based on configuration
    auto parser = create_parser();
    
    // Parse each line
    for (const auto& line : preprocessed_lines) {
        try {
            auto record = parser->parse_line(line);
            
            // Add timestamp if it's not already set and if inference is enabled
            if (!record.timestamp && config_.infer_datetime) {
                if (preprocessor_) {
                    record.timestamp = preprocessor_->identify_timestamps(record);
                }
            }
            
            result.records.push_back(std::move(record));
            processed_lines_++;
        } catch (const std::exception& e) {
            // Log error and continue
            failed_lines_++;
        }
    }
    
    return result;
}

} // namespace logai 