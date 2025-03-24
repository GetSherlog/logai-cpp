#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <folly/container/F14Map.h>
#include <folly/Synchronized.h>
#include "llm_provider.h"

namespace logai {

class LLMInterface {
public:
    enum class ProviderType {
        OPENAI,
        OLLAMA,
        GEMINI,
        CUSTOM_API
    };

    LLMInterface();
    ~LLMInterface();

    // Initialize the LLM with a specific provider
    bool init(ProviderType type, const std::string& config);

    // Generate a DuckDB query based on natural language input
    std::optional<std::string> generate_query(
        const std::string& natural_language_query,
        const std::string& template_id,
        const std::vector<std::pair<std::string, std::string>>& schema);

private:
    // Thread-safe provider
    folly::Synchronized<std::unique_ptr<LLMProvider>> provider_;
    
    // Thread-safe query cache
    folly::Synchronized<folly::F14FastMap<std::string, std::string>> query_cache_;
    
    // Helper methods
    std::string build_prompt(const std::string& query,
                           const std::string& template_id,
                           const std::vector<std::pair<std::string, std::string>>& schema);
    
    // Cache key generation
    std::string generate_cache_key(const std::string& query,
                                 const std::string& template_id,
                                 const std::vector<std::pair<std::string, std::string>>& schema);
};

} // namespace logai 