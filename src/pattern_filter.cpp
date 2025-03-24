#include "pattern_filter.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace logai {

PatternFilter::PatternMatcher::PatternMatcher(const Pattern& pattern) : pattern_(pattern) {
    if (pattern.type == MatchType::REGEX) {
        std::regex::flag_type flags = std::regex::ECMAScript;
        if (!pattern.case_sensitive) {
            flags |= std::regex::icase;
        }
        regex_ = std::make_unique<std::regex>(pattern.pattern, flags);
    } else if (pattern.type == MatchType::GLOB) {
        std::regex::flag_type flags = std::regex::ECMAScript;
        if (!pattern.case_sensitive) {
            flags |= std::regex::icase;
        }
        regex_ = std::make_unique<std::regex>(globToRegex(pattern.pattern), flags);
    }
}

bool PatternFilter::PatternMatcher::matches(const std::string& text) const {
    bool match = false;
    
    switch (pattern_.type) {
        case MatchType::REGEX:
        case MatchType::GLOB:
            match = std::regex_search(text, *regex_);
            break;
            
        case MatchType::EXACT:
            match = pattern_.case_sensitive ? 
                text == pattern_.pattern : 
                iequals(text, pattern_.pattern);
            break;
            
        case MatchType::CONTAINS:
            if (pattern_.case_sensitive) {
                match = text.find(pattern_.pattern) != std::string::npos;
            } else {
                std::string lower_text = text;
                std::string lower_pattern = pattern_.pattern;
                std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
                std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
                match = lower_text.find(lower_pattern) != std::string::npos;
            }
            break;
            
        case MatchType::PREFIX:
            if (pattern_.case_sensitive) {
                match = text.substr(0, pattern_.pattern.length()) == pattern_.pattern;
            } else {
                match = text.length() >= pattern_.pattern.length() &&
                       iequals(text.substr(0, pattern_.pattern.length()), pattern_.pattern);
            }
            break;
            
        case MatchType::SUFFIX:
            if (pattern_.case_sensitive) {
                match = text.length() >= pattern_.pattern.length() &&
                       text.substr(text.length() - pattern_.pattern.length()) == pattern_.pattern;
            } else {
                match = text.length() >= pattern_.pattern.length() &&
                       iequals(text.substr(text.length() - pattern_.pattern.length()), pattern_.pattern);
            }
            break;
    }
    
    return pattern_.inverse ? !match : match;
}

std::string PatternFilter::PatternMatcher::globToRegex(const std::string& glob) {
    std::stringstream regex;
    regex << "^";
    
    for (char c : glob) {
        switch (c) {
            case '*':
                regex << ".*";
                break;
            case '?':
                regex << ".";
                break;
            case '.': case '^': case '$': case '+':
            case '(': case ')': case '[': case ']':
            case '{': case '}': case '|': case '\\':
                regex << "\\" << c;
                break;
            default:
                regex << c;
                break;
        }
    }
    
    regex << "$";
    return regex.str();
}

bool PatternFilter::PatternMatcher::iequals(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

void PatternFilter::addPattern(const std::string& field, const Pattern& pattern) {
    field_patterns_[field].emplace_back(pattern);
}

void PatternFilter::addGlobalPattern(const Pattern& pattern) {
    global_patterns_.emplace_back(pattern);
}

void PatternFilter::clearFieldPatterns(const std::string& field) {
    field_patterns_.erase(field);
}

void PatternFilter::clearGlobalPatterns() {
    global_patterns_.clear();
}

void PatternFilter::clearAllPatterns() {
    field_patterns_.clear();
    global_patterns_.clear();
}

bool PatternFilter::matches(const LogParser::LogEntry& entry) const {
    // Check timestamp field
    if (!fieldMatches("timestamp", entry.timestamp)) {
        return false;
    }
    
    // Check level field
    if (!fieldMatches("level", entry.level)) {
        return false;
    }
    
    // Check message field
    if (!fieldMatches("message", entry.message)) {
        return false;
    }
    
    // Check all other fields
    for (const auto& [field, value] : entry.fields) {
        if (!fieldMatches(field, value)) {
            return false;
        }
    }
    
    // Check global patterns
    if (!matchesGlobalPatterns(entry)) {
        return false;
    }
    
    return true;
}

std::vector<PatternFilter::Pattern> PatternFilter::getFieldPatterns(const std::string& field) const {
    std::vector<Pattern> patterns;
    auto it = field_patterns_.find(field);
    if (it != field_patterns_.end()) {
        patterns.reserve(it->second.size());
        for (const auto& matcher : it->second) {
            patterns.push_back(matcher.getPattern());
        }
    }
    return patterns;
}

std::vector<PatternFilter::Pattern> PatternFilter::getGlobalPatterns() const {
    std::vector<Pattern> patterns;
    patterns.reserve(global_patterns_.size());
    for (const auto& matcher : global_patterns_) {
        patterns.push_back(matcher.getPattern());
    }
    return patterns;
}

bool PatternFilter::fieldMatches(const std::string& field_name, const std::string& field_value) const {
    auto it = field_patterns_.find(field_name);
    if (it == field_patterns_.end()) {
        return true;  // No patterns for this field
    }
    
    for (const auto& matcher : it->second) {
        if (!matcher.matches(field_value)) {
            return false;
        }
    }
    
    return true;
}

bool PatternFilter::matchesGlobalPatterns(const LogParser::LogEntry& entry) const {
    if (global_patterns_.empty()) {
        return true;  // No global patterns
    }
    
    for (const auto& matcher : global_patterns_) {
        // Try to match against timestamp
        if (matcher.matches(entry.timestamp)) {
            continue;
        }
        
        // Try to match against level
        if (matcher.matches(entry.level)) {
            continue;
        }
        
        // Try to match against message
        if (matcher.matches(entry.message)) {
            continue;
        }
        
        // Try to match against any field
        bool found = false;
        for (const auto& [field, value] : entry.fields) {
            if (matcher.matches(value)) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            return false;
        }
    }
    
    return true;
}

} // namespace logai 