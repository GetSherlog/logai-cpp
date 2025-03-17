#pragma once

#include <string>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace logai {

/**
 * Utility class for loading environment variables from .env file
 */
class EnvSetup {
public:
    /**
     * Load environment variables from a .env file
     * 
     * @param env_file Path to the .env file
     * @return true if successful, false otherwise
     */
    static bool load_env_file(const std::string& env_file = ".env") {
        std::ifstream file(env_file);
        if (!file.is_open()) {
            spdlog::error("Could not open .env file: {}", env_file);
            return false;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            // Find equals sign
            size_t pos = line.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            
            // Extract key and value
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes if present
            if (value.size() >= 2 && 
                ((value.front() == '"' && value.back() == '"') || 
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }
            
            // Set environment variable
            #ifdef _WIN32
            _putenv_s(key.c_str(), value.c_str());
            #else
            setenv(key.c_str(), value.c_str(), 1);
            #endif
            
            env_vars_[key] = value;
        }
        
        return true;
    }
    
    /**
     * Get an environment variable
     * 
     * @param key Name of the environment variable
     * @return Value of the environment variable, or empty string if not found
     */
    static std::string get_env(const std::string& key) {
        const char* value = std::getenv(key.c_str());
        return value ? value : "";
    }
    
    /**
     * Set an environment variable
     * 
     * @param key Name of the environment variable
     * @param value Value to set
     */
    static void set_env(const std::string& key, const std::string& value) {
        #ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
        #else
        setenv(key.c_str(), value.c_str(), 1);
        #endif
        
        env_vars_[key] = value;
    }
    
private:
    static inline std::unordered_map<std::string, std::string> env_vars_;
};

} // namespace logai 