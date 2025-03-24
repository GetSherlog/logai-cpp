#include "llm_interface.h"
#include "openai_provider.h"
#include "llama_provider.h"
#include <spdlog/spdlog.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace logai {

namespace {
    // CURL callback to handle response
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
}

LLMInterface::LLMInterface() = default;

LLMInterface::~LLMInterface() = default;

bool LLMInterface::init(ProviderType type, const std::string& config) {
    try {
        std::unique_ptr<LLMProvider> new_provider;
        
        switch (type) {
            case ProviderType::OPENAI:
                new_provider = std::make_unique<OpenAIProvider>();
                break;
            case ProviderType::LLAMA:
                new_provider = std::make_unique<LlamaProvider>();
                break;
            default:
                spdlog::error("Unknown provider type");
                return false;
        }
        
        if (!new_provider->init(config)) {
            spdlog::error("Failed to initialize provider");
            return false;
        }
        
        auto provider = provider_.wlock();
        *provider = std::move(new_provider);
        
        spdlog::info("LLM interface initialized with provider: {}", provider->get()->get_model_name());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize LLM interface: {}", e.what());
        return false;
    }
}

std::string LLMInterface::generate_cache_key(
    const std::string& query,
    const std::string& template_id,
    const std::vector<std::pair<std::string, std::string>>& schema) {
    
    std::stringstream ss;
    ss << query << "|" << template_id << "|";
    for (const auto& [column, type] : schema) {
        ss << column << ":" << type << ";";
    }
    return ss.str();
}

std::string LLMInterface::build_prompt(const std::string& query,
                                     const std::string& template_id,
                                     const std::vector<std::pair<std::string, std::string>>& schema) {
    std::string prompt = "You are a SQL query generator. Generate a DuckDB query for the following request.\n\n";
    prompt += "Template ID: " + template_id + "\n\n";
    prompt += "Schema:\n";
    
    for (const auto& [column, type] : schema) {
        prompt += column + " (" + type + ")\n";
    }
    
    prompt += "\nUser Query: " + query + "\n\n";
    prompt += "Generate a DuckDB query that will return the requested information. "
              "Only return the SQL query, no explanations.\n";
    
    return prompt;
}

std::optional<std::string> LLMInterface::generate_query(
    const std::string& natural_language_query,
    const std::string& template_id,
    const std::vector<std::pair<std::string, std::string>>& schema) {
    
    // Check cache first
    std::string cache_key = generate_cache_key(natural_language_query, template_id, schema);
    {
        auto cache = query_cache_.rlock();
        auto it = cache->find(cache_key);
        if (it != cache->end()) {
            return it->second;
        }
    }

    // Check if provider is initialized
    {
        auto provider = provider_.rlock();
        if (!provider->get()) {
            spdlog::error("LLM interface not initialized");
            return std::nullopt;
        }
    }

    try {
        // Build the prompt
        std::string prompt = build_prompt(natural_language_query, template_id, schema);
        
        // Generate response using provider
        auto provider = provider_.rlock();
        auto response = provider->get()->generate(prompt);
        
        if (!response) {
            spdlog::error("Failed to generate response");
            return std::nullopt;
        }
        
        // Clean up the query (remove any markdown formatting)
        std::string query = *response;
        query.erase(std::remove(query.begin(), query.end(), '`'), query.end());
        
        // Cache the result
        {
            auto cache = query_cache_.wlock();
            (*cache)[cache_key] = query;
        }
        
        return query;
    } catch (const std::exception& e) {
        spdlog::error("Failed to generate query: {}", e.what());
        return std::nullopt;
    }
}

} // namespace logai 