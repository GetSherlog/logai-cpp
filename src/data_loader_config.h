#pragma once

#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <tuple>
#include <folly/container/F14Map.h>

namespace logai {

struct DataLoaderConfig {
    std::string file_path;
    std::string log_type = "CSV";
    std::vector<std::string> dimensions;
    std::string datetime_format = "%Y-%m-%dT%H:%M:%SZ";
    bool infer_datetime = false;
    std::string log_pattern = ""; // Default regex pattern for log parsing
    
    // Performance configuration
    size_t num_threads = std::thread::hardware_concurrency();
    size_t batch_size = 10000;
    bool use_memory_mapping = true;
    bool use_simd = true;
    
    // Preprocessor configuration
    bool enable_preprocessing = false;
    folly::F14FastMap<std::string, std::string> custom_delimiters_regex;
    std::vector<std::tuple<std::string, std::string>> custom_replace_list;

    // DRAIN parser configuration
    int drain_depth = 4;
    double drain_similarity_threshold = 0.5;
    int drain_max_children = 100;
};
} 