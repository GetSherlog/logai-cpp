#ifndef LOGAI_LOG_RECORD_OBJECT_H
#define LOGAI_LOG_RECORD_OBJECT_H

#include <string>
#include <folly/container/F14Map.h>
#include <folly/FBString.h>

namespace logai {

class LogRecordObject {
public:
    std::string body;
    std::string template_str;
    folly::F14FastMap<folly::fbstring, folly::fbstring> fields;

    bool has_field(const std::string& key) const {
        return fields.find(key) != fields.end();
    }

    std::string get_field(const std::string& key) const {
        auto it = fields.find(key);
        return it != fields.end() ? it->second.toStdString() : "";
    }
};

} // namespace logai

#endif // LOGAI_LOG_RECORD_OBJECT_H 