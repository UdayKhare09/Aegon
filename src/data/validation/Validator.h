#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <concepts>
#include <regex>
#include <charconv>

namespace aegon::validation {

class ValidationRules;

template <typename T>
concept HasValidate = requires(const T& t, ValidationRules& v) {
    t.validate(v);
};

class ValidationRules {
public:
    ValidationRules() = default;

    void add_violation(std::string_view field_name, std::string message) {
        std::string full_name = prefix_.empty() ? std::string(field_name) : (prefix_ + std::string(field_name));
        violations_.emplace_back(std::move(full_name), std::move(message));
    }

    [[nodiscard]] bool has_violations() const noexcept {
        return !violations_.empty();
    }

    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& violations() const noexcept {
        return violations_;
    }

#include <glaze/glaze.hpp>
#include <unordered_map>

    struct ValidationReport {
        int status{422};
        std::string error{"Unprocessable Entity"};
        std::string message{"Validation failed"};
        std::unordered_map<std::string, std::string> violations;
    };

    /**
     * @brief Generates RFC 7807 / Spring Boot style structured 422 JSON error report.
     */
    [[nodiscard]] std::string to_json() const {
        ValidationReport report;
        report.violations.reserve(violations_.size());
        for (const auto& [field, msg] : violations_) {
            report.violations[field] = msg;
        }
        std::string out;
        std::ignore = glz::write_json(report, out);
        return out;
    }

    // Fluent field validator helper
    template <typename T>
    class FieldValidator {
    public:
        FieldValidator(ValidationRules& parent, std::string_view name, const T& val)
            : parent_(parent), name_(name), val_(val) {}

        FieldValidator& required(std::string msg = "This field is required") {
            if constexpr (is_optional_v<T>) {
                if (!val_.has_value()) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
                if (val_.empty()) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (is_vector_v<T>) {
                if (val_.empty()) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& min_len(size_t min_length, std::string msg = "") {
            if (msg.empty()) msg = "Length must be at least " + std::to_string(min_length);
            if constexpr (is_optional_v<T>) {
                if (val_.has_value() && val_->size() < min_length) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
                if (val_.size() < min_length) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& max_len(size_t max_length, std::string msg = "") {
            if (msg.empty()) msg = "Length must be at most " + std::to_string(max_length);
            if constexpr (is_optional_v<T>) {
                if (val_.has_value() && val_->size() > max_length) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
                if (val_.size() > max_length) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& email(std::string msg = "Must be a valid email address") {
            auto check = [&](std::string_view s) {
                if (s.empty()) return;
                size_t at = s.find('@');
                if (at == std::string_view::npos || at == 0 || at == s.size() - 1) {
                    parent_.add_violation(name_, msg);
                    return;
                }
                size_t dot = s.find('.', at);
                if (dot == std::string_view::npos || dot == at + 1 || dot == s.size() - 1) {
                    parent_.add_violation(name_, msg);
                }
            };

            if constexpr (is_optional_v<T>) {
                if (val_.has_value()) check(*val_);
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
                check(val_);
            }
            return *this;
        }

        template <typename Num>
        FieldValidator& min(Num min_val, std::string msg = "") {
            if (msg.empty()) msg = "Value must be at least " + std::to_string(min_val);
            if constexpr (is_optional_v<T>) {
                if (val_.has_value() && *val_ < min_val) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (std::is_arithmetic_v<T>) {
                if (val_ < min_val) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        template <typename Num>
        FieldValidator& max(Num max_val, std::string msg = "") {
            if (msg.empty()) msg = "Value must be at most " + std::to_string(max_val);
            if constexpr (is_optional_v<T>) {
                if (val_.has_value() && *val_ > max_val) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (std::is_arithmetic_v<T>) {
                if (val_ > max_val) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        template <typename Predicate>
        FieldValidator& custom(Predicate&& pred, std::string msg = "Custom validation failed") {
            if constexpr (is_optional_v<T>) {
                if (val_.has_value() && !pred(*val_)) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else {
                if (!pred(val_)) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& not_nil(std::string msg = "Must not be nil") {
            if constexpr (is_optional_v<T>) {
                if (val_.has_value()) {
                    if constexpr (requires { val_->is_nil(); }) {
                        if (val_->is_nil()) {
                            parent_.add_violation(name_, std::move(msg));
                        }
                    }
                }
            } else if constexpr (requires { val_.is_nil(); }) {
                if (val_.is_nil()) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& positive(std::string msg = "Must be positive") {
            if constexpr (is_optional_v<T>) {
                if (val_.has_value()) {
                    if constexpr (requires { val_->is_positive(); }) {
                        if (!val_->is_positive()) parent_.add_violation(name_, std::move(msg));
                    } else if constexpr (std::is_arithmetic_v<typename T::value_type>) {
                        if (*val_ <= 0) parent_.add_violation(name_, std::move(msg));
                    }
                }
            } else if constexpr (requires { val_.is_positive(); }) {
                if (!val_.is_positive()) {
                    parent_.add_violation(name_, std::move(msg));
                }
            } else if constexpr (std::is_arithmetic_v<T>) {
                if (val_ <= 0) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& past(std::string msg = "Must be in the past") {
            if constexpr (is_optional_v<T>) {
                if (val_.has_value()) {
                    if constexpr (requires { *val_ < T::value_type::now(); }) {
                        if (!(*val_ < T::value_type::now())) parent_.add_violation(name_, std::move(msg));
                    }
                }
            } else if constexpr (requires { val_ < T::now(); }) {
                if (!(val_ < T::now())) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

        FieldValidator& future(std::string msg = "Must be in the future") {
            if constexpr (is_optional_v<T>) {
                if (val_.has_value()) {
                    if constexpr (requires { *val_ > T::value_type::now(); }) {
                        if (!(*val_ > T::value_type::now())) parent_.add_violation(name_, std::move(msg));
                    }
                }
            } else if constexpr (requires { val_ > T::now(); }) {
                if (!(val_ > T::now())) {
                    parent_.add_violation(name_, std::move(msg));
                }
            }
            return *this;
        }

    private:
        ValidationRules& parent_;
        std::string_view name_;
        const T& val_;

        template <typename U> struct is_optional : std::false_type {};
        template <typename U> struct is_optional<std::optional<U>> : std::true_type {};
        template <typename U> static constexpr bool is_optional_v = is_optional<U>::value;

        template <typename U> struct is_vector : std::false_type {};
        template <typename U> struct is_vector<std::vector<U>> : std::true_type {};
        template <typename U> static constexpr bool is_vector_v = is_vector<U>::value;
    };

    template <typename T>
    FieldValidator<T> field(std::string_view name, const T& value) {
        return FieldValidator<T>(*this, name, value);
    }

    /**
     * @brief Cascade validation into a nested DTO object.
     */
    template <typename NestedT>
        requires HasValidate<NestedT>
    void nested(std::string_view name, const NestedT& obj) {
        std::string prev = prefix_;
        prefix_ = prefix_.empty() ? (std::string(name) + ".") : (prefix_ + std::string(name) + ".");
        obj.validate(*this);
        prefix_ = prev;
    }

    /**
     * @brief Cascade validation into an optional nested DTO object (validates only if present).
     */
    template <typename NestedT>
        requires HasValidate<NestedT>
    void nested(std::string_view name, const std::optional<NestedT>& obj) {
        if (obj.has_value()) {
            nested(name, *obj);
        }
    }

    /**
     * @brief Cascade validation across every item in an array/vector of nested DTOs.
     */
    template <typename NestedT>
        requires HasValidate<NestedT>
    void nested_each(std::string_view name, const std::vector<NestedT>& items) {
        for (size_t i = 0; i < items.size(); ++i) {
            std::string prev = prefix_;
            std::string indexed = std::string(name) + "[" + std::to_string(i) + "].";
            prefix_ = prefix_.empty() ? indexed : (prefix_ + indexed);
            items[i].validate(*this);
            prefix_ = prev;
        }
    }

private:
    std::string prefix_;
    std::vector<std::pair<std::string, std::string>> violations_;
};

} // namespace aegon::validation
