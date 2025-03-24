#pragma once

#include <string>
#include <regex>
#include <vector>
#include <memory>
#include <unordered_map>
#include "log_parser.h"

namespace logai {

class PatternFilter {
public:
    enum class MatchType {
        REGEX,      // Regular expression matching
        GLOB,       // Glob pattern matching (*, ?)
        EXACT,      // Exact string matching
        CONTAINS,   // Substring matching
        PREFIX,     // Prefix matching
        SUFFIX      // Suffix matching
    };
    
    struct Pattern {
        std::string pattern;
        MatchType type;
        bool case_sensitive;
        bool inverse;  // If true, match when pattern does NOT match
    };
    
    // Add a pattern to match against a specific field
    void addPattern(const std::string& field, const Pattern& pattern);
    
    // Add a pattern to match against any field
    void addGlobalPattern(const Pattern& pattern);
    
    // Remove all patterns for a specific field
    void clearFieldPatterns(const std::string& field);
    
    // Remove all global patterns
    void clearGlobalPatterns();
    
    // Remove all patterns
    void clearAllPatterns();
    
    // Check if a log entry matches all patterns
    bool matches(const LogParser::LogEntry& entry) const;
    
    // Get current patterns for a field
    std::vector<Pattern> getFieldPatterns(const std::string& field) const;
    
    // Get current global patterns
    std::vector<Pattern> getGlobalPatterns() const;
    
private:
    class PatternMatcher {
    public:
        explicit PatternMatcher(const Pattern& pattern);
        bool matches(const std::string& text) const;
        Pattern getPattern() const { return pattern_; }
        
    private:
        Pattern pattern_;
        std::unique_ptr<std::regex> regex_;
        
        // Convert glob pattern to regex pattern
        static std::string globToRegex(const std::string& glob);
        
        // Case-insensitive string comparison
        static bool iequals(const std::string& a, const std::string& b);
    };
    
    std::unordered_map<std::string, std::vector<PatternMatcher>> field_patterns_;
    std::vector<PatternMatcher> global_patterns_;
    
    // Check if a single field matches all its patterns
    bool fieldMatches(const std::string& field_name, const std::string& field_value) const;
    
    // Check if any field matches a global pattern
    bool matchesGlobalPatterns(const LogParser::LogEntry& entry) const;
};

} // namespace logai 