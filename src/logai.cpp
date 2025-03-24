#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cxxopts.hpp>
#include "drain_parser.h"
#include "gemini_vectorizer.h"
#include "template_store.h"
#include "feature_extractor.h"
#include "file_data_loader.h"
#include "dbscan_clustering_kdtree.h"
#include "one_class_svm.h"
#include "duckdb_store.h"
#include "llm_interface.h"

class LogAI {
public:
    LogAI() {
        parser = std::make_unique<DrainParser>();
        vectorizer = std::make_unique<GeminiVectorizer>();
        templateStore = std::make_unique<TemplateStore>();
        featureExtractor = std::make_unique<FeatureExtractor>();
        anomalyDetector = std::make_unique<OneClassSVM>();
        duckdbStore = std::make_unique<DuckDBStore>();
        llmInterface = std::make_unique<LLMInterface>();
    }

    void parse(const std::string& file_path) {
        std::cout << "Parsing log file: " << file_path << std::endl;
        
        FileDataLoader loader(file_path);
        std::vector<std::string> log_lines;
        loader.loadData(log_lines);

        std::cout << "Found " << log_lines.size() << " log entries" << std::endl;
        
        // Parse logs and extract templates
        for (const auto& line : log_lines) {
            auto template_info = parser->parse(line);
            templateStore->addTemplate(template_info);
            
            // Store in DuckDB
            if (!template_info.template_str.empty()) {
                // Initialize table if needed
                std::vector<std::string> columns = {"timestamp", "level", "body"};
                std::vector<std::string> types = {"VARCHAR", "VARCHAR", "VARCHAR"};
                duckdbStore->init_template_table(template_info.template_str, columns, types);
                
                // Insert record
                duckdbStore->insert_log_record(template_info.template_str, template_info);
            }
        }

        std::cout << "Extracted " << templateStore->getTemplateCount() << " unique templates" << std::endl;
        
        // Save templates to disk for future use
        templateStore->saveTemplates("templates.json");
    }

    void search(const std::string& query, const std::string& file_path) {
        std::cout << "Searching for: " << query << " in " << file_path << std::endl;
        
        // First, find similar templates using vector similarity
        auto similar_templates = templateStore->search(query, 5);  // Get top 5 similar templates
        
        if (similar_templates.empty()) {
            std::cout << "No similar templates found." << std::endl;
            return;
        }

        // For each similar template, generate and execute a DuckDB query
        for (const auto& [template_id, similarity] : similar_templates) {
            auto template_str = templateStore->get_template(template_id);
            if (!template_str) continue;

            // Get schema for this template
            auto schema = duckdbStore->get_schema(*template_str);
            
            // Generate DuckDB query using LLM
            auto db_query = llmInterface->generate_query(query, *template_str, schema);
            if (!db_query) {
                std::cout << "Failed to generate query for template: " << *template_str << std::endl;
                continue;
            }

            // Execute query
            auto results = duckdbStore->execute_query(*db_query);
            
            // Display results with color
            std::cout << "\n\033[1;34mTemplate: " << *template_str << " (similarity: " << similarity << ")\033[0m\n";
            for (const auto& row : results) {
                std::cout << "\033[1;32m[";
                for (size_t i = 0; i < row.size(); ++i) {
                    if (i > 0) std::cout << " | ";
                    std::cout << row[i];
                }
                std::cout << "]\033[0m\n";
            }
        }
    }

    void analyze(const std::string& file_path) {
        std::cout << "Analyzing patterns in: " << file_path << std::endl;
        
        FileDataLoader loader(file_path);
        std::vector<std::string> log_lines;
        loader.loadData(log_lines);

        // Extract features from logs
        auto features = featureExtractor->extractFeatures(log_lines);

        // Analyze patterns
        std::cout << "\nPattern Analysis Results:" << std::endl;
        std::cout << "------------------------" << std::endl;
        
        // Display most common patterns
        auto patterns = featureExtractor->getCommonPatterns(features);
        std::cout << "\nMost Common Patterns:" << std::endl;
        for (const auto& [pattern, count] : patterns) {
            std::cout << pattern << ": " << count << " occurrences" << std::endl;
        }

        // Display temporal patterns
        auto temporal_patterns = featureExtractor->getTemporalPatterns(features);
        std::cout << "\nTemporal Patterns:" << std::endl;
        for (const auto& [time, count] : temporal_patterns) {
            std::cout << time << ": " << count << " events" << std::endl;
        }
    }

    void detectAnomalies(const std::string& file_path) {
        std::cout << "Detecting anomalies in: " << file_path << std::endl;
        
        FileDataLoader loader(file_path);
        std::vector<std::string> log_lines;
        loader.loadData(log_lines);

        // Extract features
        auto features = featureExtractor->extractFeatures(log_lines);

        // Train anomaly detector
        anomalyDetector->train(features);

        // Detect anomalies
        std::vector<bool> is_anomaly = anomalyDetector->detect(features);

        // Display results
        std::cout << "\nAnomaly Detection Results:" << std::endl;
        std::cout << "------------------------" << std::endl;
        
        int anomaly_count = 0;
        for (size_t i = 0; i < log_lines.size(); ++i) {
            if (is_anomaly[i]) {
                std::cout << "ANOMALY: " << log_lines[i] << std::endl;
                anomaly_count++;
            }
        }

        std::cout << "\nFound " << anomaly_count << " anomalies out of " 
                  << log_lines.size() << " total entries" << std::endl;
    }

private:
    std::unique_ptr<DrainParser> parser;
    std::unique_ptr<GeminiVectorizer> vectorizer;
    std::unique_ptr<TemplateStore> templateStore;
    std::unique_ptr<FeatureExtractor> featureExtractor;
    std::unique_ptr<OneClassSVM> anomalyDetector;
    std::unique_ptr<DuckDBStore> duckdbStore;
    std::unique_ptr<LLMInterface> llmInterface;
};

int main(int argc, char* argv[]) {
    cxxopts::Options options("logai", "AI-powered log analysis tool");
    
    options.add_options()
        ("p,parse", "Parse a log file", cxxopts::value<std::string>())
        ("s,search", "Search logs semantically", cxxopts::value<std::string>())
        ("a,analyze", "Analyze log patterns", cxxopts::value<std::string>())
        ("d,detect", "Detect anomalies", cxxopts::value<std::string>())
        ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    LogAI logai;

    try {
        if (result.count("parse")) {
            logai.parse(result["parse"].as<std::string>());
        }
        else if (result.count("search")) {
            logai.search(result["search"].as<std::string>(), "");
        }
        else if (result.count("analyze")) {
            logai.analyze(result["analyze"].as<std::string>());
        }
        else if (result.count("detect")) {
            logai.detectAnomalies(result["detect"].as<std::string>());
        }
        else {
            std::cout << "Please specify a command. Use --help for usage information." << std::endl;
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 