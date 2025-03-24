#pragma once
#include "llm_provider.h"
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <llama.h>

namespace logai {

class LlamaProvider : public LLMProvider {
public:
    LlamaProvider();
    ~LlamaProvider() override;

    bool init(const std::string& config) override;
    std::optional<std::string> generate(
        const std::string& prompt,
        const std::string& system_prompt = "") override;
    std::string get_model_name() const override;

private:
    struct Config {
        std::string model_path;
        int context_size;
        int num_threads;
        float temperature;
        int max_tokens;
    };

    // Thread-safe configuration
    folly::Synchronized<std::unique_ptr<Config>> config_;
    
    // Thread-safe model context
    folly::Synchronized<std::unique_ptr<llama_context, decltype(&llama_free)>> ctx_;
    
    // Thread-safe response cache
    folly::Synchronized<folly::F14FastMap<std::string, std::string>> response_cache_;
    
    // Helper methods
    std::string generate_cache_key(const std::string& prompt, const std::string& system_prompt);
    bool load_model();
    void cleanup();
};

} // namespace logai 