#include "time_filter.h"
#include <regex>
#include <sstream>
#include <iomanip>

namespace logai {

namespace {
    // Regular expression for parsing ISO 8601 timestamps
    const std::regex iso8601_regex{
        R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,3}))?(?:Z|([+-]\d{2}):?(\d{2}))?$)"};
}

TimeFilter::TimeFilter(const std::optional<std::string>& start_time,
                     const std::optional<std::string>& end_time) {
    if (start_time) {
        setStartTime(*start_time);
    }
    if (end_time) {
        setEndTime(*end_time);
    }
}

bool TimeFilter::passes(const std::string& timestamp) const {
    auto time_point = parseTimestamp(timestamp);
    
    if (start_time_ && time_point < *start_time_) {
        return false;
    }
    
    if (end_time_ && time_point > *end_time_) {
        return false;
    }
    
    return true;
}

void TimeFilter::setStartTime(const std::string& start_time) {
    start_time_ = parseTimestamp(start_time);
}

void TimeFilter::setEndTime(const std::string& end_time) {
    end_time_ = parseTimestamp(end_time);
}

void TimeFilter::clearStartTime() {
    start_time_ = std::nullopt;
}

void TimeFilter::clearEndTime() {
    end_time_ = std::nullopt;
}

std::optional<std::string> TimeFilter::getStartTime() const {
    if (!start_time_) {
        return std::nullopt;
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(*start_time_);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        start_time_->time_since_epoch()).count() % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms
       << 'Z';
    
    return ss.str();
}

std::optional<std::string> TimeFilter::getEndTime() const {
    if (!end_time_) {
        return std::nullopt;
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(*end_time_);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time_->time_since_epoch()).count() % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms
       << 'Z';
    
    return ss.str();
}

std::chrono::system_clock::time_point TimeFilter::parseTimestamp(const std::string& timestamp) const {
    std::smatch match;
    if (!std::regex_match(timestamp, match, iso8601_regex)) {
        throw std::runtime_error("Invalid ISO 8601 timestamp format: " + timestamp);
    }
    
    std::tm tm = {};
    tm.tm_year = std::stoi(match[1]) - 1900;
    tm.tm_mon = std::stoi(match[2]) - 1;
    tm.tm_mday = std::stoi(match[3]);
    tm.tm_hour = std::stoi(match[4]);
    tm.tm_min = std::stoi(match[5]);
    tm.tm_sec = std::stoi(match[6]);
    
    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    
    // Add milliseconds if present
    if (match[7].matched) {
        std::string ms_str = match[7].str() + std::string(3 - match[7].length(), '0');
        time_point += std::chrono::milliseconds(std::stoi(ms_str));
    }
    
    // Handle timezone offset
    if (match[8].matched) {
        int offset_hours = std::stoi(match[8]);
        int offset_minutes = std::stoi(match[9]);
        auto offset = std::chrono::hours(std::abs(offset_hours)) + std::chrono::minutes(offset_minutes);
        
        if (offset_hours < 0) {
            time_point += offset;
        } else {
            time_point -= offset;
        }
    }
    
    return time_point;
}

} // namespace logai 