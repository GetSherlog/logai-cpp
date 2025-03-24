#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <Eigen/Dense>

namespace logai {

/**
 * @brief Parameters for One-Class SVM based Anomaly Detector
 * 
 * For more explanations about the parameters see:
 * https://scikit-learn.org/stable/modules/generated/sklearn.svm.OneClassSVM.html
 */
struct OneClassSVMParams {
    /**
     * @brief Default constructor with recommended parameter values
     */
    OneClassSVMParams() = default;

    /**
     * @brief Constructor with custom parameter values
     * 
     * @param kernel Specifies the kernel type to be used
     * @param degree Degree of the polynomial kernel function ('poly')
     * @param gamma Kernel coefficient for 'rbf', 'poly' and 'sigmoid'
     * @param coef0 Independent term in kernel function, significant in 'poly' and 'sigmoid'
     * @param tol Tolerance for stopping criterion
     * @param nu An upper bound on the fraction of training errors and a lower bound of the fraction of support vectors
     * @param shrinking Whether to use the shrinking heuristic
     * @param cache_size Specify the size of the kernel cache (in MB)
     * @param verbose Enable verbose output
     */
    OneClassSVMParams(const std::string& kernel, int degree, const std::string& gamma,
                      float coef0, float tol, float nu, bool shrinking,
                      float cache_size, bool verbose)
        : kernel(kernel), degree(degree), gamma(gamma), coef0(coef0),
          tol(tol), nu(nu), shrinking(shrinking), cache_size(cache_size),
          verbose(verbose) {}

    std::string kernel = "linear";
    int degree = 3;
    std::string gamma = "auto";
    float coef0 = 0.0f;
    float tol = 1e-3f;
    float nu = 0.5f;
    bool shrinking = true;
    float cache_size = 200.0f;
    bool verbose = false;
};

/**
 * @brief One-Class SVM based Anomaly Detector
 * 
 * This is an implementation of the One-Class SVM algorithm for anomaly detection.
 * One-Class SVM is an unsupervised algorithm that learns a decision function for
 * novelty detection: classifying new data as similar or different to the training set.
 */
class OneClassSVMDetector {
public:
    /**
     * @brief Constructor with parameters
     * 
     * @param params The parameters for the One-Class SVM algorithm
     */
    explicit OneClassSVMDetector(const OneClassSVMParams& params);

    /**
     * @brief Fit method to train the One-Class SVM on log data
     * 
     * @param log_features Training log features as a matrix where each row represents a data point
     * @return The anomaly scores of the training dataset
     */
    Eigen::VectorXd fit(const Eigen::MatrixXd& log_features);

    /**
     * @brief Predict method to detect anomalies using One-Class SVM model on test log data
     * 
     * @param log_features Test log features data as a matrix where each row represents a data point
     * @return A vector of the predicted anomaly scores (-1 for outliers, 1 for inliers)
     */
    Eigen::VectorXd predict(const Eigen::MatrixXd& log_features) const;
    
    /**
     * @brief Calculate anomaly scores (decision function values) for test log data
     * 
     * @param log_features Test log features data as a matrix where each row represents a data point
     * @return A vector of decision function values (the lower, the more abnormal)
     */
    Eigen::VectorXd score_samples(const Eigen::MatrixXd& log_features) const;

private:
    /**
     * @brief Compute the kernel function between two vectors
     * 
     * @param x First vector
     * @param y Second vector
     * @return The kernel value
     */
    double kernel_function(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const;

    /**
     * @brief Compute RBF kernel between two vectors: exp(-gamma * ||x-y||^2)
     */
    double rbf_kernel(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const;
    
    /**
     * @brief Compute linear kernel between two vectors: x^T * y
     */
    double linear_kernel(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const;
    
    /**
     * @brief Compute polynomial kernel: (gamma * x^T * y + coef0)^degree
     */
    double poly_kernel(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const;
    
    /**
     * @brief Compute sigmoid kernel: tanh(gamma * x^T * y + coef0)
     */
    double sigmoid_kernel(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const;

    /**
     * @brief The parameters for the One-Class SVM algorithm
     */
    OneClassSVMParams params_;
    
    /**
     * @brief Support vectors from the training data
     */
    Eigen::MatrixXd support_vectors_;
    
    /**
     * @brief Coefficients for the support vectors in the decision function
     */
    Eigen::VectorXd dual_coefs_;
    
    /**
     * @brief The bias/offset term in the decision function
     */
    double rho_;
    
    /**
     * @brief The gamma parameter used in the kernel function
     */
    double gamma_value_;
};

} // namespace logai 