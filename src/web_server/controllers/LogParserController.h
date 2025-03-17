#pragma once

#include "ApiController.h"
#include "drain_parser.h"
#include "file_data_loader.h"
#include "preprocessor.h"
#include "template_manager.h"
#include "template_store.h"
#include <drogon/HttpController.h>
#include <memory>
#include <vector>
#include <string>
#include <arrow/api.h>
#include "regex_parser.h"
#include "data_loader_config.h"
#include <unordered_map>

namespace logai {
namespace web {

/**
 * @class LogParserController
 * @brief Controller for log parsing related endpoints
 */
class LogParserController : public drogon::HttpController<LogParserController, false>,
                         public ApiController {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(LogParserController::parseFile, "/api/parser/file", drogon::Post);
    ADD_METHOD_TO(LogParserController::searchTemplates, "/api/parser/search", drogon::Post);
    ADD_METHOD_TO(LogParserController::getTemplateAttributes, "/api/parser/attributes", drogon::Get);
    METHOD_LIST_END

    LogParserController();
    ~LogParserController() = default;

    /**
     * @brief Parse logs from a file using the appropriate parser based on file type
     * @param req HTTP request with file path and parser config
     * @param callback Response callback
     */
    void parseFile(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief Search for log templates similar to a query
     * @param req HTTP request with search query
     * @param callback Response callback
     */
    void searchTemplates(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
                        
    /**
     * @brief Get attributes for a specific template
     * @param req HTTP request with template ID
     * @param callback Response callback
     */
    void getTemplateAttributes(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief Add a parsed log and its template to the template store
     * @param template_id The template ID
     * @param template_str The template string
     * @param log The parsed log record
     * @param attributes Map of attribute names to values extracted from this log
     */
    void addTemplateToStore(int template_id, const std::string& template_str, 
                           const LogRecordObject& log,
                           const std::unordered_map<std::string, std::string>& attributes = {});
                           
    /**
     * @brief Get the attributes dataframe for a specific template
     * @param template_id The template ID
     * @return Arrow table containing attributes for the template, or nullptr if not found
     */
    std::shared_ptr<arrow::Table> getAttributesForTemplate(int template_id) const;
    
    /**
     * @brief Get the template store
     * @return Reference to the template store
     */
    const TemplateStore& getTemplateStore() const { return templateStore_; }

private:
    std::shared_ptr<ApiController> apiController_;
    std::shared_ptr<DrainParser> drainParser_;
    std::shared_ptr<TemplateManager> templateManager_;
    
    // Template store for storing and searching templates
    TemplateStore templateStore_;
    
    // Store attributes for each template in Arrow tables
    std::unordered_map<int, std::shared_ptr<arrow::Table>> templateAttributes_;
    
    // Helper method to build Arrow table from attributes
    std::shared_ptr<arrow::Table> buildAttributesTable(
        const std::vector<std::unordered_map<std::string, std::string>>& attributes);
        
    // Helper to extract attributes from a log based on its template
    std::unordered_map<std::string, std::string> extractAttributes(
        const LogRecordObject& log, const std::string& template_str);
};

} // namespace web
} // namespace logai 