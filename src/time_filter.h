#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace logai {

class TimeFilter {
public:
    // Initialize filter with optional start and end times in ISO 8601 format
    TimeFilter(const std::optional<std::string>& start_time = std::nullopt,
              const std::optional<std::string>& end_time = std::nullopt);
    
    // Check if a timestamp (in ISO 8601 format) passes the filter
    bool passes(const std::string& timestamp) const;
    
    // Set start time (in ISO 8601 format)
    void setStartTime(const std::string& start_time);
    
    // Set end time (in ISO 8601 format)
    void setEndTime(const std::string& end_time);
    
    // Clear start time filter
    void clearStartTime();
    
    // Clear end time filter
    void clearEndTime();
    
    // Get current start time
    std::optional<std::string> getStartTime() const;
    
    // Get current end time
    std::optional<std::string> getEndTime() const;
    
private:
    // Convert ISO 8601 timestamp to system time point
    std::chrono::system_clock::time_point parseTimestamp(const std::string& timestamp) const;
    
    std::optional<std::chrono::system_clock::time_point> start_time_;
    std::optional<std::chrono::system_clock::time_point> end_time_;
};

} // namespace logai 