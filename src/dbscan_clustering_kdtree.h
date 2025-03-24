#pragma once

#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <queue>
#include <cmath>
#include <string>

namespace logai {

// Forward declarations for k-d tree node
struct KDNode;

/**
 * @brief A simple k-d tree implementation for faster nearest-neighbor searches
 */
class KDTree {
public:
    /**
     * @brief Construct a new KDTree object from data points
     * 
     * @param data The input data as vector of vectors
     */
    explicit KDTree(const std::vector<std::vector<float>>& data);
    
    /**
     * @brief Destructor to clean up the tree
     */
    ~KDTree();
    
    /**
     * @brief Find all points within a given radius of a query point
     * 
     * @param query The query point
     * @param radius The search radius
     * @return A vector of indices representing points within the radius
     */
    std::vector<size_t> radius_search(const std::vector<float>& query, float radius) const;

private:
    /**
     * @brief Build a k-d tree recursively
     * 
     * @param points Indices of points to build the tree from
     * @param depth Current depth in the tree
     * @return KDNode* The root node of the (sub)tree
     */
    KDNode* build_tree(const std::vector<size_t>& points, int depth);
    
    /**
     * @brief Search for points within a radius recursively
     * 
     * @param node Current node in the search
     * @param query The query point
     * @param radius The search radius
     * @param depth Current depth in the tree
     * @param results Vector to store indices of points within the radius
     */
    void search_radius(const KDNode* node, const std::vector<float>& query, 
                      float radius, int depth, std::vector<size_t>& results) const;
    
    /**
     * @brief Compute the squared Euclidean distance between two points
     * 
     * @param p1 First point
     * @param p2 Second point
     * @return The squared distance
     */
    float squared_distance(const std::vector<float>& p1, const std::vector<float>& p2) const;
    
    /**
     * @brief The data points stored as a vector of vectors
     */
    std::vector<std::vector<float>> data_;
    
    /**
     * @brief The root of the k-d tree
     */
    KDNode* root_;
    
    /**
     * @brief The dimensionality of the data
     */
    size_t dimensions_;
};

/**
 * @brief Parameters for the DBSCAN clustering algorithm with k-d tree optimization
 */
struct DbScanKDTreeParams {
    /**
     * @brief Default constructor with recommended parameter values
     */
    DbScanKDTreeParams() = default;
    
    /**
     * @brief Constructor with custom parameter values
     * 
     * @param eps The maximum distance between two samples for one to be considered as in the neighborhood of the other
     * @param min_samples The number of samples in a neighborhood for a point to be considered as a core point
     */
    DbScanKDTreeParams(float eps, int min_samples)
        : eps(eps), min_samples(min_samples) {}
    
    /**
     * @brief The maximum distance between two samples for one to be considered as in the neighborhood of the other
     */
    float eps = 0.5f;
    
    /**
     * @brief The number of samples in a neighborhood for a point to be considered as a core point
     */
    int min_samples = 5;
};

/**
 * @brief DBSCAN clustering algorithm implementation with k-d tree optimization
 * 
 * This implementation uses a k-d tree for faster nearest-neighbor searches,
 * which significantly improves performance on large datasets.
 */
class DbScanClusteringKDTree {
public:
    /**
     * @brief Constructor with parameters
     * 
     * @param params The parameters for the DBSCAN algorithm
     */
    explicit DbScanClusteringKDTree(const DbScanKDTreeParams& params);
    
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
     * @brief Find all points in the epsilon neighborhood of a point using the k-d tree
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
    DbScanKDTreeParams params_;
    
    /**
     * @brief The input data as a vector of vectors
     */
    std::vector<std::vector<float>> data_;
    
    /**
     * @brief The assigned cluster labels (where -1 represents noise)
     */
    std::vector<int> labels_;
    
    /**
     * @brief The k-d tree for efficient nearest-neighbor searches
     */
    std::unique_ptr<KDTree> kdtree_;
};

} // namespace logai 