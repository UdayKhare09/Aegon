#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "core/Task.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <array>
#include <optional>

namespace aegon::http {

using Handler = std::function<core::Task<void>(Context&)>;

enum class RadixNodeType : uint8_t {
    Static,
    Param,
    Wildcard
};

struct RadixNode {
    std::string prefix;
    RadixNodeType type{RadixNodeType::Static};
    std::string param_name;
    std::vector<std::unique_ptr<RadixNode>> children;
    std::array<Handler, 9> handlers{};
    std::array<bool, 9> has_handler{};
    bool has_any_handler{false};

    RadixNode() = default;
    explicit RadixNode(std::string_view p, RadixNodeType t = RadixNodeType::Static, std::string_view pname = "")
        : prefix(p), type(t), param_name(pname) {}
};

class RadixTree {
public:
    RadixTree();
    ~RadixTree() = default;

    RadixTree(const RadixTree&) = delete;
    RadixTree& operator=(const RadixTree&) = delete;
    RadixTree(RadixTree&&) noexcept = default;
    RadixTree& operator=(RadixTree&&) noexcept = default;

    /**
     * @brief Inserts a route into the Radix Tree with O(k) prefix compression.
     */
    void insert(Method method, std::string_view pattern, Handler handler);

    struct MatchResult {
        const Handler* handler{nullptr};
        bool route_found{false};
        bool method_not_allowed{false};
    };

    /**
     * @brief Resolves a request path in O(k) time and binds parameters directly into Request.
     */
    [[nodiscard]] MatchResult match(Request& req) const;

private:
    std::unique_ptr<RadixNode> root_;

    bool match_node(const RadixNode* node, std::string_view path,
                    std::vector<std::pair<std::string_view, std::string_view>>& params,
                    const RadixNode*& matched_node) const;
};

} // namespace aegon::http
