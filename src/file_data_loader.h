#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <filesystem>
#include <unordered_set>
#include <optional>
#include <arrow/api.h>
#include <arrow/status.h>
#include "data_loader_config.h"
#include "log_record.h"
#include "memory_mapped_file.h"
#include "thread_safe_queue.h"
#include "log_parser.h"
#include "preprocessor.h"

// Forward declaration for Arrow Table
namespace arrow {
    class Table;
    class Status;
}

namespace logai {

/**
 * @brief Configuration for file data loader
 */
struct FileDataLoaderConfig {
    std::string encoding = "utf-8";
    std::string delimiter = ",";
    bool has_header = true;
    bool logical_lines = false;
    bool decompress = false;
    size_t buffer_size = 8192;
    size_t max_line_length = 1024 * 1024;  // 1MB
    std::string format = "logfmt";  // Default format
};

/**
 * @brief Batch of log lines for parallel processing
 */
struct LogBatch {
    size_t id;
    std::vector<std::string> lines;
};

/**
 * @brief Processed batch of log data
 */
struct ProcessedBatch {
    size_t id;
    std::vector<LogRecordObject> records;
};

/**
 * @brief High-performance file data loader for log analysis
 */
class FileDataLoader {
public:
    explicit FileDataLoader(const std::string& filepath, const FileDataLoaderConfig& config = FileDataLoaderConfig());

    // Configuration setters
    void setEncoding(const std::string& encoding) { config_.encoding = encoding; }
    void setDelimiter(const std::string& delimiter) { config_.delimiter = delimiter; }
    void setHasHeader(bool has_header) { config_.has_header = has_header; }
    void setLogicalLines(bool logical_lines) { config_.logical_lines = logical_lines; }
    void setDecompress(bool decompress) { config_.decompress = decompress; }
    void setBufferSize(size_t buffer_size) { config_.buffer_size = buffer_size; }
    void setMaxLineLength(size_t max_line_length) { config_.max_line_length = max_line_length; }
    void setFormat(const std::string& format);

    // Load all data at once
    void loadData(std::vector<LogParser::LogEntry>& entries);

    // Stream data line by line
    void streamData(const std::function<bool(const LogParser::LogEntry&)>& callback);

    // Process data in chunks
    void processInChunks(size_t chunk_size, 
        const std::function<void(const std::vector<LogParser::LogEntry>&)>& callback);

    std::vector<LogRecordObject> load_data();
    double get_progress() const;
    
    // New Arrow-based operations
    std::shared_ptr<arrow::Table> log_to_dataframe(const std::string& filepath, const std::string& format);
    std::shared_ptr<arrow::Table> filter_dataframe(std::shared_ptr<arrow::Table> table, const std::vector<std::string>& dimensions);
    std::shared_ptr<arrow::Table> filter_dataframe(std::shared_ptr<arrow::Table> table, 
                                                  const std::string& column, 
                                                  const std::string& op, 
                                                  const std::string& value);
    arrow::Status write_to_parquet(std::shared_ptr<arrow::Table> table, const std::string& output_path);

    /**
     * @brief Apply preprocessing to a batch of log lines
     * 
     * @param log_lines The log lines to preprocess
     * @return Preprocessed log lines
     */
    std::vector<std::string> preprocess_logs(const std::vector<std::string>& log_lines);

    /**
     * @brief Extract attributes from preprocessed logs using regex patterns
     * 
     * @param log_lines The preprocessed log lines
     * @param patterns Map of attribute names to regex patterns
     * @return Table with extracted attributes
     */
    std::shared_ptr<arrow::Table> extract_attributes(
        const std::vector<std::string>& log_lines,
        const std::unordered_map<std::string, std::string>& patterns);

    /**
     * @brief Create a unified schema from multiple Arrow tables
     * @param tables The vector of Arrow tables to unify
     * @return A vector of tables with unified schemas ready for concatenation
     */
    std::vector<std::shared_ptr<arrow::Table>> unify_table_schemas(
        const std::vector<std::shared_ptr<arrow::Table>>& tables);

    // Process a single batch of log lines
    void process_batch(const LogBatch& batch, ProcessedBatch& result);
    
    /**
     * @brief Process a large log file with automatic memory management and chunking
     * 
     * @param input_file The path to the input log file
     * @param parser_type The type of parser to use (drain, json, csv, regex)
     * @param memory_limit_mb Maximum memory limit in megabytes
     * @param chunk_size Initial chunk size (number of lines per batch)
     * @param force_chunking Force processing in chunks even if file is small
     * @return std::shared_ptr<arrow::Table> The final Arrow table with parsed results
     */
    std::shared_ptr<arrow::Table> process_large_file(const std::string& input_file,
                                                  const std::string& parser_type,
                                                  size_t memory_limit_mb = 2000,
                                                  size_t chunk_size = 10000,
                                                  bool force_chunking = false);

private:
    std::string filepath_;
    FileDataLoaderConfig config_;
    std::unique_ptr<std::istream> input_stream_;
    std::unique_ptr<LogParser> parser_;

    // Initialize input stream based on file type
    void initInputStream();

    // Initialize parser based on format
    void initParser();

    // Handle compressed files
    std::unique_ptr<std::istream> openCompressedFile();

    // Handle logical lines
    std::string readLogicalLine();
    bool isLogicalLineContinuation(const std::string& line);
    std::string handleIndentationContinuation(const std::string& line, std::string& current_line);
    std::string handleBackslashContinuation(const std::string& line, std::string& current_line);

    // Utility functions
    bool isCompressedFile() const;
    std::string getFileExtension() const;
    void validateEncoding() const;

    DataLoaderConfig config_;
    std::atomic<size_t> total_lines_read_{0};
    std::atomic<size_t> processed_lines_{0};
    std::atomic<size_t> failed_lines_{0};
    std::atomic<bool> running_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<size_t> total_batches_{0};
    
    // Preprocessor (created on demand only if enabled)
    std::unique_ptr<Preprocessor> preprocessor_;
    
    // Adaptive batch sizing parameters
    std::atomic<size_t> current_batch_size_{100};  // Default batch size
    std::atomic<size_t> max_batch_size_{1000};     // Maximum batch size
    std::atomic<size_t> min_batch_size_{10};       // Minimum batch size
    std::atomic<size_t> queue_high_watermark_{200}; // Queue size to trigger batch size reduction
    std::atomic<size_t> queue_low_watermark_{10};   // Queue size to trigger batch size increase
    std::atomic<bool> memory_pressure_{false};      // Flag for memory pressure detection
    
    // Multi-threading components
    ThreadSafeQueue<LogBatch> batch_queue_;
    ThreadSafeQueue<ProcessedBatch> processed_queue_;
    std::vector<std::thread> worker_threads_;
    
    std::vector<LogRecordObject> read_logs(const std::string& filepath);
    std::vector<LogRecordObject> read_csv(const std::string& filepath);
    std::vector<LogRecordObject> read_tsv(const std::string& filepath);
    std::vector<LogRecordObject> read_json(const std::string& filepath);
    
    std::vector<LogRecordObject> create_log_record_objects(
        const std::vector<std::vector<std::string>>& data, 
        const std::vector<std::string>& headers);
    
    std::vector<std::string> simd_parse_csv_line(const std::string& line, char delimiter);
    bool simd_pattern_search(const std::string& line, const std::string& pattern);
    
    void read_file_by_chunks(
        const std::string& filepath, 
        const std::function<void(const std::string&)>& callback);
    
    void read_file_memory_mapped(
        const std::string& filepath,
        std::function<void(std::string_view)> callback);
    
    void reader_thread(const std::string& filepath);
    void worker_thread(ThreadSafeQueue<LogBatch>& input_queue, ThreadSafeQueue<ProcessedBatch>& output_queue);
    void collector_thread();
    
    ProcessedBatch process_batch(const LogBatch& batch, const std::string& log_format = "");
    std::unique_ptr<LogParser> create_parser();
    void producer_thread(MemoryMappedFile& file, ThreadSafeQueue<LogBatch>& input_queue, std::atomic<size_t>& total_batches);
    void consumer_thread(size_t num_threads, ThreadSafeQueue<ProcessedBatch>& output_queue, std::vector<LogRecordObject>& results, std::atomic<size_t>& total_batches);

    // Read file line by line with callback
    void read_file_line_by_line(const std::string& filepath, 
                               std::function<void(std::string_view)> callback);
                               
    // Memory monitoring functions
    size_t get_current_memory_usage() const;
    void adjust_batch_size(ThreadSafeQueue<LogBatch>& queue);
    bool detect_memory_pressure() const;
    void process_in_chunks(const std::string& filepath, size_t chunk_size, const std::string& output_dir);
    
    // Initialize preprocessor if needed
    void init_preprocessor();
};

} 