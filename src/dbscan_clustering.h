#pragma once

#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <string>

namespace logai {

/**
 * @brief Parameters for the DBSCAN clustering algorithm
 */
struct DbScanParams {
    /**
     * @brief Default constructor with recommended parameter values
     */
    DbScanParams() = default;

    /**
     * @brief Constructor with custom parameter values
     * 
     * @param eps The maximum distance between two samples for one to be considered as in the neighborhood of the other
     * @param min_samples The number of samples in a neighborhood for a point to be considered as a core point
     * @param metric The metric to use when calculating distance between instances (currently only 'euclidean' is supported)
     */
    DbScanParams(float eps, int min_samples, const std::string& metric = "euclidean")
        : eps(eps), min_samples(min_samples), metric(metric) {}

    /**
     * @brief The maximum distance between two samples for one to be considered as in the neighborhood of the other
     */
    float eps = 0.5f;

    /**
     * @brief The number of samples in a neighborhood for a point to be considered as a core point
     */
    int min_samples = 5;

    /**
     * @brief The distance metric to use (currently only 'euclidean' is supported)
     */
    std::string metric = "euclidean";
};

/**
 * @brief DBSCAN clustering algorithm implementation
 * 
 * DBSCAN (Density-Based Spatial Clustering of Applications with Noise) is a 
 * density-based clustering algorithm that finds a number of clusters starting 
 * from the estimated density distribution of corresponding nodes.
 */
class DbScanClustering {
public:
    /**
     * @brief Constructor with parameters
     * 
     * @param params The parameters for the DBSCAN algorithm
     */
    explicit DbScanClustering(const DbScanParams& params);

    /**
     * @brief Fit the DBSCAN model according to the given data
     * 
     * @param data Input data as a vector of vectors where each inner vector represents a point
     */
    void fit(const std::vector<std::vector<float>>& data);

    /**
     * @brief Get the cluster labels after fitting the model
     * 
     * @return A vector of cluster labels where -1 represents noise
     */
    std::vector<int> get_labels() const;

private:
    /**
     * @brief Compute the distance between two points
     * 
     * @param p1 First point
     * @param p2 Second point
     * @return The distance between the points based on the specified metric
     */
    float compute_distance(const std::vector<float>& p1, const std::vector<float>& p2) const;

    /**
     * @brief Find all points in the epsilon neighborhood of a point
     * 
     * @param point_idx The index of the point to find neighbors for
     * @return A vector of indices representing neighboring points
     */
    std::vector<size_t> region_query(size_t point_idx) const;

    /**
     * @brief Expand a cluster from a core point
     * 
     * @param point_idx The index of the core point
     * @param neighbors The neighbors of the core point
     * @param cluster_id The ID of the cluster being expanded
     */
    void expand_cluster(size_t point_idx, const std::vector<size_t>& neighbors, int cluster_id);

    /**
     * @brief The parameters for the DBSCAN algorithm
     */
    DbScanParams params_;

    /**
     * @brief The input data as a vector of vectors
     */
    std::vector<std::vector<float>> data_;

    /**
     * @brief The assigned cluster labels (where -1 represents noise)
     */
    std::vector<int> labels_;
};

} // namespace logai 