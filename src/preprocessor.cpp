#include "preprocessor.h"
#include <memory>
#include <thread>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <folly/container/F14Map.h>

namespace logai {

PreprocessorConfig::PreprocessorConfig(
    folly::F14FastMap<std::string, std::string> custom_delimiters_regex,
    std::vector<std::tuple<std::string, std::string>> custom_replace_list,
    bool use_simd
) : custom_delimiters_regex(std::move(custom_delimiters_regex)),
    custom_replace_list(std::move(custom_replace_list)),
    use_simd(use_simd) {}

Preprocessor::Preprocessor(const PreprocessorConfig& config) : config_(config) {
    initialize_patterns();
}

void Preprocessor::initialize_patterns() {
    // Precompile all regex patterns for better performance
    if (!config_.custom_delimiters_regex.empty()) {
        for (const auto& [pattern, _] : config_.custom_delimiters_regex) {
            try {
                delimiter_regexes_.emplace_back(pattern, std::regex::optimize);
            } catch (const std::regex_error& e) {
                throw std::runtime_error("Invalid delimiter regex pattern: " + pattern + 
                                         " Error: " + std::to_string(e.code()));
            }
        }
    }

    if (!config_.custom_replace_list.empty()) {
        for (const auto& [pattern, replacement] : config_.custom_replace_list) {
            try {
                replacement_regexes_.emplace_back(
                    std::regex(pattern, std::regex::optimize), 
                    replacement
                );
            } catch (const std::regex_error& e) {
                throw std::runtime_error("Invalid replacement regex pattern: " + pattern + 
                                         " Error: " + std::to_string(e.code()));
            }
        }
    }
}

std::tuple<std::string, folly::F14FastMap<std::string, std::vector<std::string>>> 
Preprocessor::clean_log_line(std::string_view logline) {
    if (config_.use_simd) {
        return clean_log_line_simd(logline);
    }

    std::string cleaned_log(logline);
    folly::F14FastMap<std::string, std::vector<std::string>> terms;

    // Apply delimiter regex replacements
    for (const auto& regex : delimiter_regexes_) {
        cleaned_log = std::regex_replace(cleaned_log, regex, " ");
    }

    // Apply custom replacements and extract terms
    for (const auto& [regex, replacement] : replacement_regexes_) {
        // Extract terms before replacing
        std::vector<std::string> matches;
        auto words_begin = std::sregex_iterator(cleaned_log.begin(), cleaned_log.end(), regex);
        auto words_end = std::sregex_iterator();
        
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            matches.push_back(i->str());
        }
        
        if (!matches.empty()) {
            terms[replacement] = std::move(matches);
        }
        
        // Perform the replacement
        cleaned_log = std::regex_replace(cleaned_log, regex, replacement);
    }

    return {cleaned_log, terms};
}

std::vector<char> Preprocessor::prepare_delimiter_char_set() const {
    // Extract common delimiter characters from regex patterns
    // This is a simplification - it works for simple character classes
    // but won't handle complex regex patterns
    std::vector<char> delimiters;
    
    // Common delimiter characters
    const std::vector<char> common_delimiters = {
        ',', ';', ':', '|', '\t', '[', ']', '{', '}', '(', ')', '<', '>'
    };
    
    // Add common delimiters first
    for (char c : common_delimiters) {
        delimiters.push_back(c);
    }
    
    // Try to extract simple characters from regex patterns
    for (const auto& regex : delimiter_regexes_) {
        // This is a simplification - in a real implementation, 
        // you would extract characters from the regex pattern
    }
    
    return delimiters;
}

std::tuple<std::string, folly::F14FastMap<std::string, std::vector<std::string>>> 
Preprocessor::clean_log_line_simd(std::string_view logline) {
    if (logline.empty()) {
        return {std::string(), {}};
    }
    
    folly::F14FastMap<std::string, std::vector<std::string>> terms;
    
    // First, apply SIMD-optimized delimiter replacements
    std::vector<char> delimiters = prepare_delimiter_char_set();
    std::string cleaned_log = SimdStringOps::replace_chars(logline, delimiters, ' ');
    
    // Normalize consecutive spaces to a single space
    bool prev_was_space = false;
    std::string normalized;
    normalized.reserve(cleaned_log.size());
    
    for (char c : cleaned_log) {
        if (c == ' ') {
            if (!prev_was_space) {
                normalized.push_back(c);
                prev_was_space = true;
            }
        } else {
            normalized.push_back(c);
            prev_was_space = false;
        }
    }
    
    // Trim leading/trailing whitespace using SIMD
    cleaned_log = SimdStringOps::trim(normalized);
    
    // For complex patterns that can't be handled with SIMD, fall back to regex
    // This is for the custom replacements and term extraction
    for (const auto& [regex, replacement] : replacement_regexes_) {
        // Extract terms before replacing
        std::vector<std::string> matches;
        auto words_begin = std::sregex_iterator(cleaned_log.begin(), cleaned_log.end(), regex);
        auto words_end = std::sregex_iterator();
        
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            matches.push_back(i->str());
        }
        
        if (!matches.empty()) {
            terms[replacement] = std::move(matches);
        }
        
        // Perform the replacement
        cleaned_log = std::regex_replace(cleaned_log, regex, replacement);
    }
    
    return {cleaned_log, terms};
}

std::tuple<std::vector<std::string>, folly::F14FastMap<std::string, std::vector<std::vector<std::string>>>>
Preprocessor::clean_log_batch(const std::vector<std::string>& loglines) {
    if (config_.use_simd) {
        return clean_log_batch_simd(loglines);
    }

    const size_t num_lines = loglines.size();
    std::vector<std::string> cleaned_logs(num_lines);
    folly::F14FastMap<std::string, std::vector<std::vector<std::string>>> all_terms;
    
    // Initialize term vectors for each replacement pattern
    for (const auto& [_, replacement] : replacement_regexes_) {
        all_terms[replacement].resize(num_lines);
    }
    
    // Process logs in parallel if there are enough lines
    const size_t num_threads = std::min(
        std::max(1U, std::thread::hardware_concurrency()),
        static_cast<unsigned int>(num_lines > 1000 ? 8 : 1) // Use multithreading only for large batches
    );
    
    // Batch size per thread
    const size_t batch_size = (num_lines + num_threads - 1) / num_threads;
    
    // Lambda for processing a range of logs
    auto process_range = [&](size_t start, size_t end) {
        for (size_t i = start; i < end && i < num_lines; ++i) {
            auto [cleaned, extracted_terms] = clean_log_line(loglines[i]);
            cleaned_logs[i] = std::move(cleaned);
            
            // Store extracted terms
            for (auto& [key, values] : extracted_terms) {
                all_terms[key][i] = std::move(values);
            }
        }
    };
    
    if (num_threads > 1 && num_lines > 1000) {
        // Use parallel processing for large batches
        std::vector<std::thread> threads;
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start = t * batch_size;
            size_t end = std::min(start + batch_size, num_lines);
            threads.emplace_back(process_range, start, end);
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        // Use single-threaded processing for small batches
        process_range(0, num_lines);
    }
    
    return {cleaned_logs, all_terms};
}

std::tuple<std::vector<std::string>, folly::F14FastMap<std::string, std::vector<std::vector<std::string>>>>
Preprocessor::clean_log_batch_simd(const std::vector<std::string>& loglines) {
    const size_t num_lines = loglines.size();
    std::vector<std::string> cleaned_logs(num_lines);
    folly::F14FastMap<std::string, std::vector<std::vector<std::string>>> all_terms;
    
    // Initialize term vectors for each replacement pattern
    for (const auto& [_, replacement] : replacement_regexes_) {
        all_terms[replacement].resize(num_lines);
    }
    
    // Prepare common delimiter set once for all lines
    std::vector<char> delimiters = prepare_delimiter_char_set();
    
    // Process logs in parallel if there are enough lines
    const size_t num_threads = std::min(
        std::max(1U, std::thread::hardware_concurrency()),
        static_cast<unsigned int>(num_lines > 1000 ? 8 : 1) // Use multithreading only for large batches
    );
    
    // Batch size per thread
    const size_t batch_size = (num_lines + num_threads - 1) / num_threads;
    
    // Lambda for processing a range of logs with SIMD operations
    auto process_range = [&](size_t start, size_t end) {
        for (size_t i = start; i < end && i < num_lines; ++i) {
            auto [cleaned, extracted_terms] = clean_log_line_simd(loglines[i]);
            cleaned_logs[i] = std::move(cleaned);
            
            // Store extracted terms
            for (auto& [key, values] : extracted_terms) {
                all_terms[key][i] = std::move(values);
            }
        }
    };
    
    if (num_threads > 1 && num_lines > 1000) {
        // Use parallel processing for large batches
        std::vector<std::thread> threads;
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start = t * batch_size;
            size_t end = std::min(start + batch_size, num_lines);
            threads.emplace_back(process_range, start, end);
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        // Use single-threaded processing for small batches
        process_range(0, num_lines);
    }
    
    return {cleaned_logs, all_terms};
}

std::optional<std::chrono::system_clock::time_point> 
Preprocessor::identify_timestamps(const LogRecordObject& logrecord) {
    std::vector<std::pair<std::string, std::string>> timestamp_formats = {
        {R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})?)", "%Y-%m-%dT%H:%M:%S"},
        {R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})", "%Y-%m-%d %H:%M:%S"},
        {R"(\d{2}/\w{3}/\d{4}:\d{2}:\d{2}:\d{2})", "%d/%b/%Y:%H:%M:%S"},
        {R"(\w{3} \d{2} \d{2}:\d{2}:\d{2})", "%b %d %H:%M:%S"}
    };
    
    // Try to find a timestamp in the log body
    for (const auto& [regex_str, format] : timestamp_formats) {
        std::regex regex_pattern(regex_str);
        std::smatch match;
        const std::string& body = logrecord.body; // Create a reference to avoid temporary
        if (std::regex_search(body, match, regex_pattern)) {
            std::istringstream ss(match.str());
            std::tm tm = {};
            ss >> std::get_time(&tm, format.c_str());
            if (ss.fail()) {
                continue;
            }
            
            auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            return time_point;
        }
    }
    
    // Try to find timestamp in attributes
    for (const auto& [key, value] : logrecord.fields) {
        if (key == "timestamp" || key == "time" || key == "date" || 
            key == "datetime" || key == "created_at") {
            for (const auto& [regex_str, format] : timestamp_formats) {
                std::regex regex_pattern(regex_str);
                std::smatch match;
                std::string value_str = value.toStdString();
                if (std::regex_search(value_str, match, regex_pattern)) {
                    std::istringstream ss(match.str());
                    std::tm tm = {};
                    ss >> std::get_time(&tm, format.c_str());
                    if (ss.fail()) {
                        continue;
                    }
                    
                    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    return time_point;
                }
            }
        }
    }
    
    return std::nullopt;
}

} // namespace logai 