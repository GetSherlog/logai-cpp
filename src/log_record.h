#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <folly/container/F14Map.h>
#include <folly/FBString.h>

namespace logai {

class LogRecordObject {
public:
    std::string body;
    std::string template_str;
    folly::F14FastMap<folly::fbstring, folly::fbstring> fields;
    std::optional<std::string> severity;
    std::optional<std::chrono::system_clock::time_point> timestamp;
    std::string level;
    std::string message;

    bool has_field(const std::string& key) const {
        return fields.find(folly::fbstring(key)) != fields.end();
    }

    std::string get_field(const std::string& key) const {
        auto it = fields.find(folly::fbstring(key));
        return it != fields.end() ? it->second.toStdString() : "";
    }

    void set_field(const std::string& key, const std::string& value) {
        fields[folly::fbstring(key)] = folly::fbstring(value);
    }
};

} 