#pragma once
#include "llm_provider.h"
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace logai {

class OpenAIProvider : public LLMProvider {
public:
    OpenAIProvider();
    ~OpenAIProvider() override;

    bool init(const std::string& config) override;
    std::optional<std::string> generate(
        const std::string& prompt,
        const std::string& system_prompt = "") override;
    std::string get_model_name() const override;

private:
    struct Config {
        std::string api_key;
        std::string model;
        std::string endpoint;
    };

    // Thread-safe configuration
    folly::Synchronized<std::unique_ptr<Config>> config_;
    
    // Thread-safe response cache
    folly::Synchronized<folly::F14FastMap<std::string, std::string>> response_cache_;
    
    // Helper methods
    std::string build_request(const std::string& prompt, const std::string& system_prompt);
    std::string generate_cache_key(const std::string& prompt, const std::string& system_prompt);
};

} // namespace logai 