#include "template_manager.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace logai {

TemplateManager::TemplateManager(const std::string& template_store_path) {
    auto path = template_store_path_.wlock();
    *path = template_store_path;
}

bool TemplateManager::save_templates(const TemplateStore& store, const std::string& path) {
    // Get the target path in a thread-safe manner
    std::string target_path;
    {
        auto stored_path = template_store_path_.rlock();
        target_path = path.empty() ? *stored_path : path;
    }
    
    try {
        bool result = store.save(target_path);
        if (result) {
            spdlog::info("Templates saved to {}", target_path);
        } else {
            spdlog::error("Failed to save templates to {}", target_path);
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Error saving templates: {}", e.what());
        return false;
    }
}

bool TemplateManager::load_templates(TemplateStore& store, const std::string& path) {
    // Get the target path in a thread-safe manner
    std::string target_path;
    {
        auto stored_path = template_store_path_.rlock();
        target_path = path.empty() ? *stored_path : path;
    }
    
    // Check if the file exists
    if (!fs::exists(target_path)) {
        spdlog::warn("Template file doesn't exist: {}", target_path);
        return false;
    }
    
    try {
        bool result = store.load(target_path);
        if (result) {
            spdlog::info("Templates loaded from {}", target_path);
        } else {
            spdlog::error("Failed to load templates from {}", target_path);
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Error loading templates: {}", e.what());
        return false;
    }
}

std::string TemplateManager::get_template_store_path() const {
    auto path = template_store_path_.rlock();
    return *path;
}

void TemplateManager::set_template_store_path(const std::string& path) {
    auto stored_path = template_store_path_.wlock();
    *stored_path = path;
}

} // namespace logai