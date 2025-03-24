/**
 * @file logbert_vectorizer.h
 * @brief High-performance C++ implementation of LogBERT vectorizer for log data
 */

#pragma once

// Standard library includes
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logai {

/**
 * @brief Configuration for the LogBERTVectorizer
 */
struct LogBERTVectorizerConfig {
    std::string model_name = "bert-base-uncased";  ///< Name of the BERT model to use
    int max_token_len = 384;                       ///< Maximum sequence length
    int max_vocab_size = 5000;                     ///< Size of the vocabulary
    std::vector<std::string> custom_tokens = {};   ///< Domain-specific tokens to add
    bool truncation = true;                        ///< Whether to truncate sequences exceeding max_token_len
    int train_batch_size = 1000;                   ///< Batch size for training the tokenizer
    int num_proc = 4;                              ///< Number of threads to use for processing
    std::string output_dir = "./";                 ///< Directory for output files
    std::string tokenizer_dirpath = "";            ///< Path to tokenizer directory
};

/**
 * @brief WordPiece tokenizer for LogBERT implementation
 */
class WordPieceTokenizer {
public:
    /**
     * @brief Construct a new WordPiece Tokenizer object
     * 
     * @param vocab_file Path to vocabulary file (if empty, will be trained)
     * @param max_vocab_size Maximum vocabulary size
     * @param custom_tokens Custom tokens to add to vocabulary
     */
    WordPieceTokenizer(const std::string& vocab_file = "", 
                      int max_vocab_size = 5000,
                      const std::vector<std::string>& custom_tokens = {});

    /**
     * @brief Train the tokenizer on a corpus
     * 
     * @param corpus Vector of strings to train on
     * @param batch_size Batch size for training
     * @param num_threads Number of threads to use
     */
    void train(const std::vector<std::string>& corpus, int batch_size = 1000, int num_threads = 4);

    /**
     * @brief Tokenize a single string
     * 
     * @param text Text to tokenize
     * @param max_len Maximum sequence length
     * @param truncation Whether to truncate if exceeding max_len
     * @param add_special_tokens Whether to add [CLS] and [SEP] tokens
     * @param padding Whether to pad sequence to max_len with padding tokens
     * @return std::vector<int> Token IDs
     */
    std::vector<int> tokenize(const std::string& text, 
                              int max_len = 384, 
                              bool truncation = true,
                              bool add_special_tokens = true,
                              bool padding = false);

    /**
     * @brief Tokenize a single string and also return attention mask
     * 
     * @param text Text to tokenize
     * @param max_len Maximum sequence length
     * @param truncation Whether to truncate if exceeding max_len
     * @param add_special_tokens Whether to add [CLS] and [SEP] tokens
     * @param padding Whether to pad sequence to max_len with padding tokens
     * @return std::pair<std::vector<int>, std::vector<int>> Token IDs and attention mask
     */
    std::pair<std::vector<int>, std::vector<int>> tokenize_with_attention(
                              const std::string& text, 
                              int max_len = 384, 
                              bool truncation = true,
                              bool add_special_tokens = true,
                              bool padding = true);

    /**
     * @brief Batch tokenize multiple strings
     * 
     * @param texts Vector of strings to tokenize
     * @param max_len Maximum sequence length
     * @param truncation Whether to truncate if exceeding max_len
     * @param add_special_tokens Whether to add [CLS] and [SEP] tokens
     * @param padding Whether to pad sequences to max_len with padding tokens
     * @param num_threads Number of threads to use
     * @return std::vector<std::vector<int>> Vector of token ID sequences
     */
    std::vector<std::vector<int>> batch_tokenize(
                              const std::vector<std::string>& texts,
                              int max_len = 384,
                              bool truncation = true,
                              bool add_special_tokens = true,
                              bool padding = false,
                              int num_threads = 4);

    /**
     * @brief Batch tokenize multiple strings and return attention masks
     * 
     * @param texts Vector of strings to tokenize
     * @param max_len Maximum sequence length
     * @param truncation Whether to truncate if exceeding max_len
     * @param add_special_tokens Whether to add [CLS] and [SEP] tokens
     * @param padding Whether to pad sequences to max_len with padding tokens
     * @param num_threads Number of threads to use
     * @return std::vector<std::pair<std::vector<int>, std::vector<int>>> Vector of token ID sequences with attention masks
     */
    std::vector<std::pair<std::vector<int>, std::vector<int>>> batch_tokenize_with_attention(
                              const std::vector<std::string>& texts,
                              int max_len = 384,
                              bool truncation = true,
                              bool add_special_tokens = true,
                              bool padding = true,
                              int num_threads = 4);

    /**
     * @brief Save tokenizer to file
     * 
     * @param path Path to save the tokenizer
     * @return true If saved successfully
     * @return false If failed to save
     */
    bool save(const std::string& path);

    /**
     * @brief Load tokenizer from file
     * 
     * @param path Path to load the tokenizer from
     * @return true If loaded successfully
     * @return false If failed to load
     */
    bool load(const std::string& path);

    /**
     * @brief Check if tokenizer is trained
     * 
     * @return true If tokenizer is trained
     * @return false If tokenizer needs training
     */
    bool is_trained() const;

    /**
     * @brief Get the padding token ID
     * 
     * @return int Padding token ID
     */
    int get_pad_token_id() const;

    /**
     * @brief Normalize text according to the tokenizer's settings
     * 
     * @param text Text to normalize
     * @param is_uncased Whether to convert to lowercase
     * @return std::string Normalized text
     */
    std::string normalize(const std::string& text, bool is_uncased = true);

private:
    std::unordered_map<std::string, int> token_to_id_;
    std::unordered_map<int, std::string> id_to_token_;
    std::vector<std::string> special_tokens_;
    int max_vocab_size_;
    bool is_trained_ = false;
    std::mutex mutex_;

    // Private helper methods
    std::vector<std::string> pre_tokenize(const std::string& text);
    std::vector<std::string> word_piece_tokenize(const std::string& word);
};

/**
 * @brief C++ implementation of LogBERT vectorizer for log data
 */
class LogBERTVectorizer {
public:
    /**
     * @brief Construct a new LogBERTVectorizer object
     * 
     * @param config Configuration for the vectorizer
     */
    explicit LogBERTVectorizer(const LogBERTVectorizerConfig& config);

    /**
     * @brief Train the tokenizer on a corpus of log lines
     * 
     * @param log_corpus Vector of log lines to train on
     */
    void fit(const std::vector<std::string>& log_corpus);

    /**
     * @brief Convert log lines into tokenized sequences (deprecated format)
     * 
     * @param log_entries Vector of log lines to tokenize
     * @return std::vector<std::vector<int>> Vector of token ID sequences
     * @deprecated Use transform_with_attention instead for full compatibility with reference implementation
     */
    std::vector<std::vector<int>> transform(const std::vector<std::string>& log_entries);

    /**
     * @brief Convert log lines into tokenized sequences with attention masks
     * 
     * @param log_entries Vector of log lines to tokenize
     * @return std::vector<std::pair<std::vector<int>, std::vector<int>>> Vector of token ID sequences with attention masks
     */
    std::vector<std::pair<std::vector<int>, std::vector<int>>> transform_with_attention(
        const std::vector<std::string>& log_entries);

    /**
     * @brief Save the trained tokenizer
     * 
     * @param path Path to save the tokenizer
     * @return true If saved successfully
     * @return false If failed to save
     */
    bool save_tokenizer(const std::string& path = "");

    /**
     * @brief Load a pre-trained tokenizer
     * 
     * @param path Path to load the tokenizer from
     * @return true If loaded successfully
     * @return false If failed to load
     */
    bool load_tokenizer(const std::string& path = "");

    /**
     * @brief Check if the tokenizer is trained
     * 
     * @return true If tokenizer is trained
     * @return false If tokenizer needs training
     */
    bool is_trained() const;

private:
    LogBERTVectorizerConfig config_;
    std::unique_ptr<WordPieceTokenizer> tokenizer_;
    std::mutex tokenizer_mutex_;
    std::vector<std::string> special_tokens_;
    
    // Private helper methods
    std::vector<std::string> _clean_dataset(const std::vector<std::string>& log_entries);
    std::pair<std::vector<int>, std::vector<int>> _tokenize_function(const std::string& log_line);
    std::unordered_set<std::string> _get_all_special_tokens() const;
    std::string _normalize_text(const std::string& text) const;
    void _process_batch(
        const std::vector<std::string>& log_entries,
        size_t start_idx,
        size_t end_idx,
        std::vector<std::pair<std::vector<int>, std::vector<int>>>& results);
};

} // namespace logai 