#pragma once
#include <string>
#include <optional>
#include <vector>
#include <memory>

namespace logai {

class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    
    // Initialize the provider with configuration
    virtual bool init(const std::string& config) = 0;
    
    // Generate a response from the model
    virtual std::optional<std::string> generate(
        const std::string& prompt,
        const std::string& system_prompt = "") = 0;
    
    // Get the model name/identifier
    virtual std::string get_model_name() const = 0;
};

} // namespace logai 