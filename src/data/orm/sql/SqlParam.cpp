#include "SqlParam.h"

namespace aegon::data::orm::sql {

SqlParam SqlParam::null() noexcept {
    SqlParam p;
    p.kind = Kind::Null;
    return p;
}

SqlParam SqlParam::text(std::string_view sv) noexcept {
    SqlParam p;
    p.kind = Kind::Text;
    p.text_view = sv;
    return p;
}

SqlParam SqlParam::text_owned(std::string s) noexcept {
    SqlParam p;
    p.kind = Kind::Text;
    p.storage = std::move(s);
    p.text_view = p.storage;
    p.owns_storage = true;
    return p;
}

SqlParam SqlParam::integer(int64_t v) noexcept {
    SqlParam p;
    p.kind = Kind::Int64;
    p.i64 = v;
    return p;
}

SqlParam SqlParam::real(double v) noexcept {
    SqlParam p;
    p.kind = Kind::Double;
    p.f64 = v;
    return p;
}

SqlParam SqlParam::boolean(bool v) noexcept {
    SqlParam p;
    p.kind = Kind::Bool;
    p.b = v;
    return p;
}

SqlParam SqlParam::bytes(std::span<const std::byte> data) noexcept {
    SqlParam p;
    p.kind = Kind::Bytes;
    p.storage.assign(reinterpret_cast<const char*>(data.data()), data.size());
    p.text_view = p.storage;
    p.owns_storage = true;
    return p;
}

std::string SqlParam::to_debug_string() const {
    switch (kind) {
        case Kind::Null:   return "<NULL>";
        case Kind::Text:   return std::string(text_view);
        case Kind::Int64:  return std::to_string(i64);
        case Kind::Double: return std::to_string(f64);
        case Kind::Bool:   return b ? "true" : "false";
        case Kind::Bytes: {
            std::string hex;
            hex.reserve(text_view.size() * 2 + 2);
            hex.append("\\x");
            static constexpr char hx[] = "0123456789abcdef";
            for (unsigned char c : text_view) {
                hex.push_back(hx[c >> 4]);
                hex.push_back(hx[c & 0xf]);
            }
            return hex;
        }
    }
    return "";
}

SqlParam::SqlParam(const SqlParam& o)
    : kind(o.kind), storage(o.storage), owns_storage(o.owns_storage), i64(o.i64) {
    text_view = owns_storage ? std::string_view(storage) : o.text_view;
}

SqlParam& SqlParam::operator=(const SqlParam& o) {
    if (this != &o) {
        kind         = o.kind;
        storage      = o.storage;
        owns_storage = o.owns_storage;
        i64          = o.i64;
        text_view    = owns_storage ? std::string_view(storage) : o.text_view;
    }
    return *this;
}

SqlParam::SqlParam(SqlParam&& o) noexcept
    : kind(o.kind), storage(std::move(o.storage)), owns_storage(o.owns_storage), i64(o.i64) {
    text_view    = owns_storage ? std::string_view(storage) : o.text_view;
    o.text_view  = {};
    o.owns_storage = false;
}

SqlParam& SqlParam::operator=(SqlParam&& o) noexcept {
    if (this != &o) {
        kind         = o.kind;
        storage      = std::move(o.storage);
        owns_storage = o.owns_storage;
        i64          = o.i64;
        text_view    = owns_storage ? std::string_view(storage) : o.text_view;
        o.text_view  = {};
        o.owns_storage = false;
    }
    return *this;
}

bool SqlParam::operator==(std::string_view sv) const noexcept {
    return to_debug_string() == sv;
}

bool SqlParam::operator==(const std::string& s) const noexcept {
    return to_debug_string() == s;
}

bool SqlParam::operator==(const char* s) const noexcept {
    return s && to_debug_string() == std::string_view(s);
}

} // namespace aegon::data::orm::sql
