#pragma once
#include "template_store.h"
#include <memory>
#include <string>
#include <folly/Synchronized.h>

namespace logai {

/**
 * TemplateManager - A thread-safe class to manage log templates separately from parsers
 *
 * This class handles saving and loading templates to/from disk,
 * allowing these operations to be decoupled from specific parser implementations.
 */
class TemplateManager {
public:
    /**
     * Constructor for TemplateManager
     *
     * @param template_store_path The default path for saving/loading templates
     */
    explicit TemplateManager(const std::string& template_store_path = "./templates.json");
    
    /**
     * Destructor
     */
    ~TemplateManager() = default;
    
    /**
     * Save templates from a TemplateStore to disk
     *
     * @param store The template store to save
     * @param path Optional path override (uses default path if not specified)
     * @return true if successful, false otherwise
     */
    bool save_templates(const TemplateStore& store, const std::string& path = "");
    
    /**
     * Load templates into a TemplateStore from disk
     *
     * @param store The template store to load into
     * @param path Optional path override (uses default path if not specified)
     * @return true if successful, false otherwise
     */
    bool load_templates(TemplateStore& store, const std::string& path = "");
    
    /**
     * Get the current template store path
     *
     * @return The current template store path
     */
    std::string get_template_store_path() const;
    
    /**
     * Set the template store path
     *
     * @param path The new template store path
     */
    void set_template_store_path(const std::string& path);
    
private:
    // Thread-safe storage for the path
    folly::Synchronized<std::string> template_store_path_;
};

} // namespace logai