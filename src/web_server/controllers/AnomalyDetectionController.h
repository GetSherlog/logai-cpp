#pragma once

#include "ApiController.h"
#include "feature_extractor.h"
#include "label_encoder.h"
#include "logbert_vectorizer.h"
#include "one_class_svm.h"
#include "dbscan_clustering.h"
#include "dbscan_clustering_kdtree.h"
#include <drogon/HttpController.h>
#include <memory>
#include <vector>
#include <string>
#include "FeatureExtractor.h"
#include "LabelEncoder.h"
#include "LogBERTVectorizer.h"
#include "OneClassSVMDetector.h"
#include "DbScanClustering.h"
#include "DbScanClusteringKDTree.h"

namespace logai {
namespace web {

/**
 * @class AnomalyDetectionController
 * @brief Controller for log anomaly detection related endpoints
 */
class AnomalyDetectionController : public drogon::HttpController<AnomalyDetectionController>, public ApiController {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AnomalyDetectionController::extractFeatures, "/api/features/extract", drogon::Post);
    ADD_METHOD_TO(AnomalyDetectionController::vectorizeLogbert, "/api/features/logbert", drogon::Post);
    ADD_METHOD_TO(AnomalyDetectionController::detectAnomaliesOcSvm, "/api/anomalies/ocsvm", drogon::Post);
    ADD_METHOD_TO(AnomalyDetectionController::clusterDbscan, "/api/anomalies/dbscan", drogon::Post);
    ADD_METHOD_TO(AnomalyDetectionController::detectAnomalies, "/api/anomalies/detect", drogon::Post);
    METHOD_LIST_END

    AnomalyDetectionController();
    ~AnomalyDetectionController() = default;

    // Implement required virtual functions from DrObjectBase
    virtual const std::string& className() const override {
        static const std::string className = "AnomalyDetectionController";
        return className;
    }

    virtual bool isClass(const std::string& className) const override {
        return className == "AnomalyDetectionController";
    }

    /**
     * @brief Extract features from log lines
     * @param req HTTP request with log lines and feature extraction config
     * @param callback Response callback
     */
    void extractFeatures(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief Vectorize log messages using LogBERT
     * @param req HTTP request with log messages
     * @param callback Response callback
     */
    void vectorizeLogbert(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief Detect anomalies using One-Class SVM
     * @param req HTTP request with feature vectors
     * @param callback Response callback
     */
    void detectAnomaliesOcSvm(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief Cluster log events using DBSCAN
     * @param req HTTP request with feature vectors
     * @param callback Response callback
     */
    void clusterDbscan(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    // Unified anomaly detection endpoint
    void detectAnomalies(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    // Helper method to parse JSON requests
    bool parseJsonBody(const drogon::HttpRequestPtr& req, nlohmann::json& jsonOut);
    
    // Helper methods to create responses
    drogon::HttpResponsePtr createJsonResponse(const nlohmann::json& json);
    drogon::HttpResponsePtr createErrorResponse(const std::string& message);

    // Components
    std::unique_ptr<FeatureExtractor> featureExtractor_;
    std::unique_ptr<LabelEncoder> labelEncoder_;
    std::unique_ptr<LogBERTVectorizer> logbertVectorizer_;
    std::unique_ptr<OneClassSVMDetector> oneClassSvm_;
    std::unique_ptr<DbScanClustering> dbscan_;
    std::unique_ptr<DbScanClusteringKDTree> dbscanKdtree_;
};

} // namespace web
} // namespace logai 