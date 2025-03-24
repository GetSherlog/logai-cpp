#pragma once
#include "llm_provider.h"
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace logai {

class ExternalAPIProvider : public LLMProvider {
public:
    enum class APIType {
        OPENAI,
        GEMINI,
        OLLAMA,
        CUSTOM
    };

    ExternalAPIProvider();
    ~ExternalAPIProvider() override;

    bool init(const std::string& config) override;
    std::optional<std::string> generate(
        const std::string& prompt,
        const std::string& system_prompt = "") override;
    std::string get_model_name() const override;

private:
    struct Config {
        APIType api_type;
        std::string api_key;
        std::string model;
        std::string endpoint;
        std::string request_format;
        std::string response_field_path;
        int timeout_ms;
    };

    // Thread-safe configuration
    folly::Synchronized<std::unique_ptr<Config>> config_;
    
    // Thread-safe response cache
    folly::Synchronized<folly::F14FastMap<std::string, std::string>> response_cache_;
    
    // Helper methods
    std::string build_request(const std::string& prompt, const std::string& system_prompt);
    std::string generate_cache_key(const std::string& prompt, const std::string& system_prompt);
    std::string extract_response(const std::string& json_response);

    // API-specific request builders
    std::string build_openai_request(const std::string& prompt, const std::string& system_prompt);
    std::string build_gemini_request(const std::string& prompt, const std::string& system_prompt);
    std::string build_ollama_request(const std::string& prompt, const std::string& system_prompt);
    std::string build_custom_request(const std::string& prompt, const std::string& system_prompt);
};

} // namespace logai 