#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <fstream>
#include <cstddef>
#include <algorithm>

namespace aegon::http {

/**
 * @brief Represents an uploaded file in a multipart/form-data request.
 *
 * All string views point directly into the connection's receive buffer (zero-copy).
 */
struct FormFile {
    std::string_view name;          ///< Form field name (e.g. "avatar")
    std::string_view filename;      ///< Client-provided filename (e.g. "profile.png")
    std::string_view content_type;  ///< MIME type (e.g. "image/png"), default "application/octet-stream"
    std::string_view data;          ///< Raw binary file contents (zero-copy)

    [[nodiscard]] size_t size() const noexcept { return data.size(); }
    [[nodiscard]] bool empty() const noexcept { return data.empty(); }

    /**
     * @brief Persists the file contents to disk.
     * @param filepath Destination filesystem path
     * @return true if written successfully, false otherwise
     */
    bool save_to(const std::string& filepath) const {
        std::ofstream ofs(filepath, std::ios::binary);
        if (!ofs) return false;
        ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
        return ofs.good();
    }
};

/**
 * @brief Represents a textual field in a multipart/form-data request.
 */
struct FormField {
    std::string_view name;   ///< Form field name (e.g. "username")
    std::string_view value;  ///< Field value (zero-copy)
};

/**
 * @brief Container for parsed multipart/form-data request bodies.
 */
class MultipartFormData {
public:
    MultipartFormData() = default;

    void add_field(std::string_view name, std::string_view value) {
        fields_.push_back(FormField{.name = name, .value = value});
    }

    void add_file(FormFile file) {
        files_.push_back(std::move(file));
    }

    /**
     * @brief Retrieve a form field value by name.
     */
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept {
        for (const auto& f : fields_) {
            if (f.name == name) return f.value;
        }
        return std::nullopt;
    }

    /**
     * @brief Retrieve an uploaded file by form field name.
     */
    [[nodiscard]] std::optional<FormFile> file(std::string_view name) const noexcept {
        for (const auto& f : files_) {
            if (f.name == name) return f;
        }
        return std::nullopt;
    }

    /**
     * @brief Returns all parsed textual fields.
     */
    [[nodiscard]] const std::vector<FormField>& fields() const noexcept { return fields_; }

    /**
     * @brief Returns all parsed files.
     */
    [[nodiscard]] const std::vector<FormFile>& files() const noexcept { return files_; }

    [[nodiscard]] bool empty() const noexcept { return fields_.empty() && files_.empty(); }

private:
    std::vector<FormField> fields_;
    std::vector<FormFile> files_;
};

/**
 * @brief Zero-copy streaming RFC 7578 / RFC 2046 multipart/form-data parser.
 */
class MultipartParser {
public:
    /**
     * @brief Extracts the multipart boundary from a Content-Type header.
     * Example: "multipart/form-data; boundary=---------------------------9747672998"
     */
    static std::string_view extract_boundary(std::string_view content_type) noexcept {
        auto b_pos = content_type.find("boundary=");
        if (b_pos == std::string_view::npos) return "";

        std::string_view boundary = content_type.substr(b_pos + 9);
        // Trim leading spaces
        while (!boundary.empty() && (boundary.front() == ' ' || boundary.front() == '\t')) {
            boundary.remove_prefix(1);
        }

        // Quoted boundary: boundary="---xyz"
        if (!boundary.empty() && boundary.front() == '"') {
            boundary.remove_prefix(1);
            auto quote_end = boundary.find('"');
            if (quote_end != std::string_view::npos) {
                boundary = boundary.substr(0, quote_end);
            }
        } else {
            // Unquoted boundary stops at semicolon or end
            auto semi = boundary.find(';');
            if (semi != std::string_view::npos) {
                boundary = boundary.substr(0, semi);
            }
            while (!boundary.empty() && (boundary.back() == ' ' || boundary.back() == '\t')) {
                boundary.remove_suffix(1);
            }
        }

        return boundary;
    }

    /**
     * @brief Parses a multipart/form-data request body using the given boundary delimiter.
     * All string views point directly into body (zero allocation for payloads).
     */
    static std::optional<MultipartFormData> parse(std::string_view content_type, std::string_view body) {
        std::string_view b_token = extract_boundary(content_type);
        if (b_token.empty() || body.empty()) return std::nullopt;

        std::string delimiter;
        delimiter.reserve(b_token.size() + 4);
        delimiter.append("--");
        delimiter.append(b_token);

        std::string end_delimiter;
        end_delimiter.reserve(delimiter.size() + 2);
        end_delimiter.append(delimiter);
        end_delimiter.append("--");

        MultipartFormData result;

        size_t pos = body.find(delimiter);
        if (pos == std::string_view::npos) return std::nullopt;

        while (pos != std::string_view::npos) {
            // Move past delimiter
            pos += delimiter.size();
            if (pos >= body.size()) break;

            // Check if final boundary: "--boundary--"
            if (body.substr(pos).starts_with("--")) {
                break;
            }

            // Expect \r\n or \n after delimiter
            if (body.substr(pos).starts_with("\r\n")) {
                pos += 2;
            } else if (body.substr(pos).starts_with("\n")) {
                pos += 1;
            } else {
                return std::nullopt;
            }

            // Find next boundary delimiter
            size_t next_boundary = body.find(delimiter, pos);
            if (next_boundary == std::string_view::npos) break;

            // The part content is between pos and next_boundary.
            // Account for trailing CRLF before next delimiter.
            size_t part_end = next_boundary;
            if (part_end >= 2 && body[part_end - 2] == '\r' && body[part_end - 1] == '\n') {
                part_end -= 2;
            } else if (part_end >= 1 && body[part_end - 1] == '\n') {
                part_end -= 1;
            }

            std::string_view part = body.substr(pos, part_end - pos);

            // Locate header / body separator: \r\n\r\n or \n\n
            size_t header_end = part.find("\r\n\r\n");
            size_t sep_len = 4;
            if (header_end == std::string_view::npos) {
                header_end = part.find("\n\n");
                sep_len = 2;
            }

            if (header_end != std::string_view::npos) {
                std::string_view headers_str = part.substr(0, header_end);
                std::string_view part_body = part.substr(header_end + sep_len);

                std::string_view name;
                std::string_view filename;
                std::string_view part_ct = "text/plain";
                bool is_file = false;

                // Parse headers line by line
                size_t h_cur = 0;
                while (h_cur < headers_str.size()) {
                    size_t line_end = headers_str.find("\r\n", h_cur);
                    size_t le_len = 2;
                    if (line_end == std::string_view::npos) {
                        line_end = headers_str.find("\n", h_cur);
                        le_len = 1;
                    }
                    std::string_view line = (line_end != std::string_view::npos)
                                                ? headers_str.substr(h_cur, line_end - h_cur)
                                                : headers_str.substr(h_cur);

                    if (starts_with_case_insensitive(line, "Content-Disposition:")) {
                        // Extract name="..."
                        name = extract_header_param(line, "name");
                        // Extract filename="..."
                        auto fn_opt = extract_header_param(line, "filename");
                        if (!fn_opt.empty()) {
                            filename = fn_opt;
                            is_file = true;
                            part_ct = "application/octet-stream";
                        }
                    } else if (starts_with_case_insensitive(line, "Content-Type:")) {
                        part_ct = trim(line.substr(13));
                    }

                    if (line_end == std::string_view::npos) break;
                    h_cur = line_end + le_len;
                }

                if (!name.empty()) {
                    if (is_file) {
                        result.add_file(FormFile{
                            .name = name,
                            .filename = filename,
                            .content_type = part_ct,
                            .data = part_body
                        });
                    } else {
                        result.add_field(name, part_body);
                    }
                }
            }

            pos = next_boundary;
        }

        return result;
    }

private:
    static inline bool starts_with_case_insensitive(std::string_view str, std::string_view prefix) noexcept {
        if (str.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            char a = str[i];
            char b = prefix[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) return false;
        }
        return true;
    }

    static inline std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    }

    static inline std::string_view extract_header_param(std::string_view header_line, std::string_view param_name) noexcept {
        size_t pos = 0;
        while (pos < header_line.size()) {
            size_t found = header_line.find(param_name, pos);
            if (found == std::string_view::npos) return "";

            // Verify prefix boundary: must be preceded by space, semicolon, or start
            if (found > 0) {
                char prev = header_line[found - 1];
                if (prev != ' ' && prev != ';' && prev != '\t') {
                    pos = found + param_name.size();
                    continue;
                }
            }

            size_t eq = found + param_name.size();
            while (eq < header_line.size() && (header_line[eq] == ' ' || header_line[eq] == '\t')) {
                eq++;
            }

            if (eq < header_line.size() && header_line[eq] == '=') {
                size_t val_start = eq + 1;
                while (val_start < header_line.size() && (header_line[val_start] == ' ' || header_line[val_start] == '\t')) {
                    val_start++;
                }
                if (val_start >= header_line.size()) return "";

                if (header_line[val_start] == '"') {
                    val_start++;
                    size_t quote_end = header_line.find('"', val_start);
                    if (quote_end != std::string_view::npos) {
                        return header_line.substr(val_start, quote_end - val_start);
                    }
                    return header_line.substr(val_start);
                } else {
                    size_t val_end = header_line.find_first_of(";\r\n \t", val_start);
                    if (val_end != std::string_view::npos) {
                        return header_line.substr(val_start, val_end - val_start);
                    }
                    return header_line.substr(val_start);
                }
            }

            pos = found + param_name.size();
        }
        return "";
    }
};

} // namespace aegon::http
