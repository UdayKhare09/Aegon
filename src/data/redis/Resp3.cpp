#include "Resp3.h"

namespace aegon::data::redis {

static bool find_crlf(std::string_view sv, size_t& pos) {
    pos = sv.find("\r\n");
    return pos != std::string_view::npos;
}

ParseStatus Resp3Parser::parse(std::string_view& in, RespValue& out) {
    if (in.empty()) return ParseStatus::NeedMoreData;

    char prefix = in[0];
    switch (prefix) {
        case '+': { // Simple string
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            out.type = RespType::SimpleString;
            out.data = std::string(in.substr(1, crlf_pos - 1));
            in.remove_prefix(crlf_pos + 2);
            return ParseStatus::Done;
        }
        case '-': { // Error (e.g. -ERR ..., -MOVED ..., -ASK ...)
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            out.type = RespType::Error;
            out.data = std::string(in.substr(1, crlf_pos - 1));
            in.remove_prefix(crlf_pos + 2);
            return ParseStatus::Done;
        }
        case ':': { // Integer
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            std::string_view num_str = in.substr(1, crlf_pos - 1);
            int64_t val = 0;
            auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
            if (ec != std::errc()) return ParseStatus::Error;
            out.type = RespType::Integer;
            out.data = val;
            in.remove_prefix(crlf_pos + 2);
            return ParseStatus::Done;
        }
        case '_': { // Null (RESP3)
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            out.type = RespType::Null;
            out.data = std::monostate{};
            in.remove_prefix(crlf_pos + 2);
            return ParseStatus::Done;
        }
        case '#': { // Boolean (RESP3: #t\r\n or #f\r\n)
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            out.type = RespType::Boolean;
            out.data = (in.size() > 1 && in[1] == 't');
            in.remove_prefix(crlf_pos + 2);
            return ParseStatus::Done;
        }
        case ',': { // Double (RESP3: ,3.14\r\n)
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            std::string d_str = std::string(in.substr(1, crlf_pos - 1));
            try {
                out.type = RespType::Double;
                out.data = std::stod(d_str);
            } catch (...) {
                return ParseStatus::Error;
            }
            in.remove_prefix(crlf_pos + 2);
            return ParseStatus::Done;
        }
        case '$': { // Bulk string
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            std::string_view len_str = in.substr(1, crlf_pos - 1);
            int64_t len = 0;
            auto [ptr, ec] = std::from_chars(len_str.data(), len_str.data() + len_str.size(), len);
            if (ec != std::errc()) return ParseStatus::Error;

            if (len == -1) { // Null bulk string
                out.type = RespType::Null;
                out.data = std::monostate{};
                in.remove_prefix(crlf_pos + 2);
                return ParseStatus::Done;
            }

            size_t total_needed = crlf_pos + 2 + static_cast<size_t>(len) + 2;
            if (in.size() < total_needed) return ParseStatus::NeedMoreData;

            out.type = RespType::BulkString;
            out.data = std::string(in.substr(crlf_pos + 2, len));
            in.remove_prefix(total_needed);
            return ParseStatus::Done;
        }
        case '*':   // Array
        case '>':   // Push (Pub/Sub)
        case '~': { // Set
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            std::string_view len_str = in.substr(1, crlf_pos - 1);
            int64_t count = 0;
            auto [ptr, ec] = std::from_chars(len_str.data(), len_str.data() + len_str.size(), count);
            if (ec != std::errc()) return ParseStatus::Error;

            if (count == -1) {
                out.type = RespType::Null;
                out.data = std::monostate{};
                in.remove_prefix(crlf_pos + 2);
                return ParseStatus::Done;
            }

            std::string_view saved_in = in;
            in.remove_prefix(crlf_pos + 2);

            RespArray arr;
            arr.reserve(count);
            for (int64_t i = 0; i < count; ++i) {
                RespValue elem;
                ParseStatus s = parse(in, elem);
                if (s != ParseStatus::Done) {
                    in = saved_in; // Rollback
                    return s;
                }
                arr.push_back(std::move(elem));
            }

            out.type = (prefix == '>') ? RespType::Push : RespType::Array;
            out.data = std::move(arr);
            return ParseStatus::Done;
        }
        case '%': { // Map (RESP3: key-value elements)
            size_t crlf_pos = 0;
            if (!find_crlf(in, crlf_pos)) return ParseStatus::NeedMoreData;
            std::string_view len_str = in.substr(1, crlf_pos - 1);
            int64_t count = 0;
            auto [ptr, ec] = std::from_chars(len_str.data(), len_str.data() + len_str.size(), count);
            if (ec != std::errc()) return ParseStatus::Error;

            std::string_view saved_in = in;
            in.remove_prefix(crlf_pos + 2);

            RespArray arr;
            arr.reserve(count * 2);
            for (int64_t i = 0; i < count * 2; ++i) {
                RespValue elem;
                ParseStatus s = parse(in, elem);
                if (s != ParseStatus::Done) {
                    in = saved_in;
                    return s;
                }
                arr.push_back(std::move(elem));
            }

            out.type = RespType::Array;
            out.data = std::move(arr);
            return ParseStatus::Done;
        }
        default:
            return ParseStatus::Error;
    }
}

std::string Resp3Serializer::serialize_command(const std::vector<std::string_view>& args) {
    std::string res;
    // Estimate size
    size_t est = 16;
    for (const auto& a : args) est += 16 + a.size();
    res.reserve(est);

    res.push_back('*');
    res.append(std::to_string(args.size()));
    res.append("\r\n");

    for (const auto& arg : args) {
        res.push_back('$');
        res.append(std::to_string(arg.size()));
        res.append("\r\n");
        res.append(arg);
        res.append("\r\n");
    }

    return res;
}

} // namespace aegon::data::redis
