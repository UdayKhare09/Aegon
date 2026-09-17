#pragma once

#include <string>
#include <vector>
#include <utility>

namespace aegon::data::redis {

struct StreamMessage {
    std::string id;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct StreamReadResult {
    std::string stream;
    std::vector<StreamMessage> messages;
};

} // namespace aegon::data::redis
