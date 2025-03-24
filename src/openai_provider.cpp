#include "openai_provider.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <regex>

using json = nlohmann::json;

namespace logai {

namespace {
    // CURL callback to handle response
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
}

OpenAIProvider::OpenAIProvider() : config_(std::make_unique<Config>()) {}

OpenAIProvider::~OpenAIProvider() = default;

bool OpenAIProvider::init(const std::string& config_json) {
    try {
        auto config = config_.wlock();
        
        // Parse JSON config
        auto j = json::parse(config_json);
        
        // Determine API format
        std::string format = j.value("api_format", "openai");
        if (format == "openai") {
            config->api_format = APIFormat::OPENAI;
        } else if (format == "ollama") {
            config->api_format = APIFormat::OLLAMA;
        } else if (format == "gemini") {
            config->api_format = APIFormat::GEMINI;
        } else if (format == "custom") {
            config->api_format = APIFormat::CUSTOM;
        } else {
            spdlog::error("Unknown API format: {}", format);
            return false;
        }
        
        // Set configuration parameters
        config->api_key = j.value("api_key", "");
        config->model = j.value("model", "gpt-3.5-turbo");
        config->endpoint = j.value("endpoint", "");
        config->response_field_path = j.value("response_field_path", "");
        config->timeout_ms = j.value("timeout_ms", 30000);
        
        // Set default endpoints based on API format
        if (config->endpoint.empty()) {
            switch (config->api_format) {
                case APIFormat::OPENAI:
                    config->endpoint = "https://api.openai.com/v1/chat/completions";
                    break;
                case APIFormat::OLLAMA:
                    config->endpoint = "http://localhost:11434/api/generate";
                    break;
                case APIFormat::GEMINI:
                    config->endpoint = "https://generativelanguage.googleapis.com/v1beta/models/" + config->model + ":generateContent";
                    break;
                case APIFormat::CUSTOM:
                    spdlog::error("Custom API format requires an endpoint");
                    return false;
            }
        }
        
        // Set default response field paths
        if (config->response_field_path.empty()) {
            switch (config->api_format) {
                case APIFormat::OPENAI:
                    config->response_field_path = "choices.0.message.content";
                    break;
                case APIFormat::OLLAMA:
                    config->response_field_path = "response";
                    break;
                case APIFormat::GEMINI:
                    config->response_field_path = "candidates.0.content.parts.0.text";
                    break;
                case APIFormat::CUSTOM:
                    spdlog::error("Custom API format requires a response_field_path");
                    return false;
            }
        }
        
        spdlog::info("Initialized LLM provider: {} with model: {}", format, config->model);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize LLM provider: {}", e.what());
        return false;
    }
}

std::string OpenAIProvider::get_model_name() const {
    auto config = config_.rlock();
    return config->model;
}

std::string OpenAIProvider::generate_cache_key(const std::string& prompt, const std::string& system_prompt) {
    return prompt + "|" + system_prompt;
}

std::string OpenAIProvider::build_request(const std::string& prompt, const std::string& system_prompt) {
    auto config = config_.rlock();
    
    switch (config->api_format) {
        case APIFormat::OPENAI:
            return build_openai_request(prompt, system_prompt);
        case APIFormat::OLLAMA:
            return build_ollama_request(prompt, system_prompt);
        case APIFormat::GEMINI:
            return build_gemini_request(prompt, system_prompt);
        case APIFormat::CUSTOM:
            return build_custom_request(prompt, system_prompt);
        default:
            spdlog::error("Unknown API format");
            return "";
    }
}

std::string OpenAIProvider::build_openai_request(const std::string& prompt, const std::string& system_prompt) {
    auto config = config_.rlock();
    
    json request;
    request["model"] = config->model;
    request["messages"] = json::array();
    
    if (!system_prompt.empty()) {
        request["messages"].push_back({
            {"role", "system"},
            {"content", system_prompt}
        });
    }
    
    request["messages"].push_back({
        {"role", "user"},
        {"content", prompt}
    });
    
    return request.dump();
}

std::string OpenAIProvider::build_ollama_request(const std::string& prompt, const std::string& system_prompt) {
    auto config = config_.rlock();
    
    json request;
    request["model"] = config->model;
    request["prompt"] = prompt;
    
    if (!system_prompt.empty()) {
        request["system"] = system_prompt;
    }
    
    return request.dump();
}

std::string OpenAIProvider::build_gemini_request(const std::string& prompt, const std::string& system_prompt) {
    auto config = config_.rlock();
    
    json request;
    request["contents"] = json::array();
    
    json content;
    content["parts"] = json::array();
    
    if (!system_prompt.empty()) {
        content["parts"].push_back({
            {"text", "System: " + system_prompt + "\n\nUser: " + prompt}
        });
    } else {
        content["parts"].push_back({
            {"text", prompt}
        });
    }
    
    request["contents"].push_back(content);
    
    return request.dump();
}

std::string OpenAIProvider::build_custom_request(const std::string& prompt, const std::string& system_prompt) {
    // Custom request format - implement based on specific needs
    auto config = config_.rlock();
    
    json request;
    request["model"] = config->model;
    request["prompt"] = prompt;
    
    if (!system_prompt.empty()) {
        request["system_prompt"] = system_prompt;
    }
    
    return request.dump();
}

std::string OpenAIProvider::extract_response(const std::string& json_response) {
    auto config = config_.rlock();
    try {
        auto j = json::parse(json_response);
        
        // Split the response_field_path by dots
        std::vector<std::string> path_parts;
        std::string path = config->response_field_path;
        std::regex re("\\.");
        std::sregex_token_iterator it(path.begin(), path.end(), re, -1);
        std::sregex_token_iterator end;
        while (it != end) {
            path_parts.push_back(*it++);
        }
        
        // Navigate through the JSON using the path
        json current = j;
        for (const auto& part : path_parts) {
            // Check if part contains array index (e.g., "choices.0")
            if (part.find_first_of("0123456789") != std::string::npos) {
                size_t pos = part.find_first_of("0123456789");
                std::string key = part.substr(0, pos);
                int index = std::stoi(part.substr(pos));
                
                if (current.contains(key) && current[key].is_array() && 
                    index >= 0 && index < current[key].size()) {
                    current = current[key][index];
                } else {
                    spdlog::error("Invalid path: {} in JSON response", part);
                    return "";
                }
            } else if (current.contains(part)) {
                current = current[part];
            } else {
                spdlog::error("Path part not found: {} in JSON response", part);
                return "";
            }
        }
        
        if (current.is_string()) {
            return current.get<std::string>();
        } else {
            return current.dump();
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse response: {}", e.what());
        return "";
    }
}

std::optional<std::string> OpenAIProvider::generate(
    const std::string& prompt,
    const std::string& system_prompt) {
    
    // Check cache first
    std::string cache_key = generate_cache_key(prompt, system_prompt);
    {
        auto cache = response_cache_.rlock();
        auto it = cache->find(cache_key);
        if (it != cache->end()) {
            return it->second;
        }
    }
    
    try {
        auto config = config_.rlock();
        
        // Initialize CURL
        CURL* curl = curl_easy_init();
        if (!curl) {
            spdlog::error("Failed to initialize CURL");
            return std::nullopt;
        }
        
        // Build request payload
        std::string request_payload = build_request(prompt, system_prompt);
        
        // Set up CURL options
        std::string response;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        if (!config->api_key.empty()) {
            std::string auth_header;
            if (config->api_format == APIFormat::OPENAI) {
                auth_header = "Authorization: Bearer " + config->api_key;
            } else if (config->api_format == APIFormat::GEMINI) {
                auth_header = "x-goog-api-key: " + config->api_key;
            } else {
                auth_header = "Authorization: Bearer " + config->api_key;
            }
            headers = curl_slist_append(headers, auth_header.c_str());
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, config->endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config->timeout_ms);
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            spdlog::error("CURL request failed: {}", curl_easy_strerror(res));
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return std::nullopt;
        }
        
        // Check HTTP response code
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            spdlog::error("HTTP request failed with code {}: {}", http_code, response);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return std::nullopt;
        }
        
        // Clean up CURL
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        // Extract text from response
        std::string text = extract_response(response);
        if (text.empty()) {
            spdlog::error("Failed to extract text from response");
            return std::nullopt;
        }
        
        // Cache the result
        {
            auto cache = response_cache_.wlock();
            (*cache)[cache_key] = text;
        }
        
        return text;
    } catch (const std::exception& e) {
        spdlog::error("Failed to generate response: {}", e.what());
        return std::nullopt;
    }
}

} // namespace logai 