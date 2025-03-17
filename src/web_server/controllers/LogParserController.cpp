#include "LogParserController.h"
#include "json_parser.h"
#include "csv_parser.h"
#include "regex_parser.h"
#include "drain_parser.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <regex>
#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/array/builder_string.h>
#include <arrow/table.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace logai {
namespace web {

LogParserController::LogParserController() {
    // Initialize DrainParser with default settings
    DataLoaderConfig config;
    drainParser_ = std::make_shared<DrainParser>(config, 4, 0.5, 100);
    
    // Initialize the TemplateManager with default path
    templateManager_ = std::make_shared<TemplateManager>("./templates.json");
    
    // Load templates if they exist
    if (fs::exists(templateManager_->get_template_store_path())) {
        try {
            templateManager_->load_templates(templateStore_);
            spdlog::info("Loaded templates from {}", templateManager_->get_template_store_path());
        } catch (const std::exception& e) {
            spdlog::error("Error loading templates: {}", e.what());
        }
    }
}

void LogParserController::parseFile(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    
    // Parse request JSON
    json requestJson;
    if (!parseJsonBody(req, requestJson)) {
        callback(createErrorResponse("Invalid JSON in request body"));
        return;
    }
    
    // Validate request
    if (!requestJson.contains("filePath") || !requestJson["filePath"].is_string()) {
        callback(createErrorResponse("Request must contain 'filePath' string"));
        return;
    }
    
    const std::string filePath = requestJson["filePath"].get<std::string>();
    
    // Check if file exists
    if (!fs::exists(filePath)) {
        callback(createErrorResponse("File not found: " + filePath, drogon::k404NotFound));
        return;
    }
    
    // Create data loader config
    DataLoaderConfig config;
    config.file_path = filePath;
    
    // Check if the request contains parser type
    std::string parserType = "drain";  // Default to drain
    if (requestJson.contains("parserType") && requestJson["parserType"].is_string()) {
        parserType = requestJson["parserType"].get<std::string>();
    }
    config.log_type = parserType;
    
    try {
        // Load the file and parse logs
        FileDataLoader dataLoader(config);
        std::vector<LogRecordObject> parsedLogs = dataLoader.load_data();
        
        // Process logs to extract and store templates
        for (auto& log : parsedLogs) {
            // Get template ID and add to template store if we have a template
            if (!log.template_str.empty()) {
                int templateId = drainParser_->get_cluster_id_for_log(log.body);
                addTemplateToStore(templateId, log.template_str, log);
            }
        }
        
        // Prepare response
        json response;
        
        // Group logs by template
        std::map<std::string, std::vector<json>> templateGroups;
        
        for (const auto& log : parsedLogs) {
            // Skip logs without templates
            if (log.template_str.empty()) continue;
            
            // Create attribute entry for this log
            json attributeEntry = log.attributes;
            // Add the original log body for reference if needed
            attributeEntry["_original"] = log.body;
            
            // Add to the template group
            templateGroups[log.template_str].push_back(attributeEntry);
        }
        
        // Convert the grouped data to the response format
        response["templates"] = json::array();
        for (const auto& [template_str, attributes] : templateGroups) {
            json templateJson;
            templateJson["template"] = template_str;
            templateJson["attributes"] = attributes;
            templateJson["count"] = attributes.size();
            response["templates"].push_back(templateJson);
        }
        
        response["totalLogs"] = parsedLogs.size();
        response["totalTemplates"] = templateGroups.size();
        
        // Save templates after processing
        templateManager_->save_templates(templateStore_);
        
        callback(createJsonResponse(response));
        
    } catch (const std::exception& e) {
        callback(createErrorResponse("Error parsing file: " + std::string(e.what())));
    }
}

void LogParserController::searchTemplates(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    
    // Parse request JSON
    json requestJson;
    if (!parseJsonBody(req, requestJson)) {
        callback(createErrorResponse("Invalid JSON in request body"));
        return;
    }
    
    // Validate request
    if (!requestJson.contains("query") || !requestJson["query"].is_string()) {
        callback(createErrorResponse("Request must contain 'query' string"));
        return;
    }
    
    std::string query = requestJson["query"].get<std::string>();
    
    // Get top_k parameter if present
    int top_k = 10;  // Default value
    if (requestJson.contains("topK") && requestJson["topK"].is_number_integer()) {
        top_k = requestJson["topK"].get<int>();
        top_k = std::max(1, std::min(100, top_k));  // Clamp between 1 and 100
    }
    
    // Search for similar templates
    auto results = templateStore_.search(query, top_k);
    
    // Prepare response
    json response;
    response["results"] = json::array();
    
    for (const auto& [template_id, similarity] : results) {
        json result;
        result["templateId"] = template_id;
        result["similarity"] = similarity;
        
        auto template_str = templateStore_.get_template(template_id);
        if (template_str) {
            result["template"] = *template_str;
        } else {
            result["template"] = nullptr;
        }
        
        // Get a sample of logs for this template (limit to 5)
        auto logs_opt = templateStore_.get_logs(template_id);
        json logSamples = json::array();
        
        if (logs_opt) {
            const auto& logs = *logs_opt;
            const size_t max_samples = 5;
            const size_t samples_to_return = std::min(logs.size(), max_samples);
            
            for (size_t i = 0; i < samples_to_return; ++i) {
                logSamples.push_back(logs[i].body);
            }
            
            result["totalLogs"] = logs.size();
        } else {
            result["totalLogs"] = 0;
        }
        
        result["logSamples"] = logSamples;
        response["results"].push_back(result);
    }
    
    response["query"] = query;
    callback(createJsonResponse(response));
}

void LogParserController::addTemplateToStore(int template_id, const std::string& template_str, const LogRecordObject& log) {
    templateStore_.add_template(template_id, template_str, log);
}

} // namespace web
} // namespace logai 