#include "http/RadixTree.h"
#include <algorithm>
#include <iostream>

namespace aegon::http {

namespace {

size_t common_prefix_length(std::string_view a, std::string_view b) noexcept {
    size_t len = 0;
    size_t max_len = std::min(a.size(), b.size());
    while (len < max_len && a[len] == b[len]) {
        ++len;
    }
    return len;
}

std::string_view normalize_path(std::string_view p) noexcept {
    if (p.empty()) return "/";
    if (p.size() > 1 && p.ends_with('/')) {
        p.remove_suffix(1);
    }
    return p;
}

} // anonymous namespace

RadixTree::RadixTree() : root_(std::make_unique<RadixNode>("/")) {}

void RadixTree::insert(Method method, std::string_view pattern, Handler handler) {
    if (pattern.empty()) pattern = "/";

    RadixNode* current = root_.get();
    std::string_view remaining = pattern;

    // Special case root pattern "/"
    if (remaining == "/") {
        size_t idx = static_cast<size_t>(method);
        if (idx < current->handlers.size()) {
            current->handlers[idx] = std::move(handler);
            current->has_handler[idx] = true;
            current->has_any_handler = true;
        }
        return;
    }

    // Advance past leading slash if root is "/"
    if (remaining.starts_with('/') && current->prefix == "/") {
        remaining.remove_prefix(1);
    }

    while (!remaining.empty()) {
        // Check for parameter segment ":name" or wildcard "*"
        if (remaining.starts_with(':')) {
            remaining.remove_prefix(1);
            size_t end_param = remaining.find('/');
            std::string_view param_name = (end_param == std::string_view::npos)
                                              ? remaining
                                              : remaining.substr(0, end_param);

            // Find existing param child
            RadixNode* param_child = nullptr;
            for (auto& c : current->children) {
                if (c->type == RadixNodeType::Param && c->param_name == param_name) {
                    param_child = c.get();
                    break;
                }
            }

            if (!param_child) {
                auto new_node = std::make_unique<RadixNode>("", RadixNodeType::Param, param_name);
                param_child = new_node.get();
                current->children.push_back(std::move(new_node));
            }

            current = param_child;
            if (end_param == std::string_view::npos) {
                break;
            }
            remaining = remaining.substr(end_param);
            continue;
        }

        if (remaining.starts_with('*')) {
            remaining.remove_prefix(1);
            std::string_view wildcard_name = remaining;

            RadixNode* wildcard_child = nullptr;
            for (auto& c : current->children) {
                if (c->type == RadixNodeType::Wildcard) {
                    wildcard_child = c.get();
                    break;
                }
            }

            if (!wildcard_child) {
                auto new_node = std::make_unique<RadixNode>("", RadixNodeType::Wildcard, wildcard_name);
                wildcard_child = new_node.get();
                current->children.push_back(std::move(new_node));
            }

            current = wildcard_child;
            break;
        }

        // Static segment insertion
        size_t next_special = remaining.find_first_of(":*");
        std::string_view static_chunk = (next_special == std::string_view::npos)
                                            ? remaining
                                            : remaining.substr(0, next_special);

        // Find child matching prefix
        RadixNode* matched_child = nullptr;
        for (auto& c : current->children) {
            if (c->type == RadixNodeType::Static && !c->prefix.empty()) {
                size_t cpl = common_prefix_length(c->prefix, static_chunk);
                if (cpl > 0) {
                    matched_child = c.get();
                    if (cpl < c->prefix.size()) {
                        // Split existing child
                        auto split_node = std::make_unique<RadixNode>(
                            c->prefix.substr(cpl), c->type, c->param_name);
                        split_node->children = std::move(c->children);
                        split_node->handlers = std::move(c->handlers);
                        split_node->has_handler = c->has_handler;
                        split_node->has_any_handler = c->has_any_handler;

                        c->prefix = c->prefix.substr(0, cpl);
                        c->children.clear();
                        c->children.push_back(std::move(split_node));
                        c->has_handler.fill(false);
                        c->has_any_handler = false;
                    }

                    static_chunk.remove_prefix(cpl);
                    remaining.remove_prefix(cpl);
                    current = c.get();
                    break;
                }
            }
        }

        if (!matched_child) {
            auto new_node = std::make_unique<RadixNode>(static_chunk, RadixNodeType::Static);
            current->children.push_back(std::move(new_node));
            current = current->children.back().get();
            remaining.remove_prefix(static_chunk.size());
        }
    }

    size_t idx = static_cast<size_t>(method);
    if (idx < current->handlers.size()) {
        current->handlers[idx] = std::move(handler);
        current->has_handler[idx] = true;
        current->has_any_handler = true;
    }
}

bool RadixTree::match_node(const RadixNode* node, std::string_view path,
                          std::vector<std::pair<std::string_view, std::string_view>>& params,
                          const RadixNode*& matched_node) const {
    if (!node) return false;

    if (node->type == RadixNodeType::Static) {
        if (!node->prefix.empty()) {
            if (!path.starts_with(node->prefix)) {
                return false;
            }
            path.remove_prefix(node->prefix.size());
        }

        if (path.empty()) {
            if (node->has_any_handler) {
                matched_node = node;
                return true;
            }
            // Allow trailing slash tolerance: if remaining is empty but child is "/" with handler
            for (const auto& c : node->children) {
                if (c->type == RadixNodeType::Static && c->prefix == "/" && c->has_any_handler) {
                    matched_node = c.get();
                    return true;
                }
            }
        }

        // Try static children first for highest specificity
        for (const auto& c : node->children) {
            if (c->type == RadixNodeType::Static) {
                if (match_node(c.get(), path, params, matched_node)) {
                    return true;
                }
            }
        }

        // Try param children
        for (const auto& c : node->children) {
            if (c->type == RadixNodeType::Param) {
                if (match_node(c.get(), path, params, matched_node)) {
                    return true;
                }
            }
        }

        // Try wildcard children
        for (const auto& c : node->children) {
            if (c->type == RadixNodeType::Wildcard) {
                if (match_node(c.get(), path, params, matched_node)) {
                    return true;
                }
            }
        }

        return false;
    }

    if (node->type == RadixNodeType::Param) {
        size_t slash = path.find('/');
        std::string_view val = (slash == std::string_view::npos) ? path : path.substr(0, slash);
        if (val.empty()) return false;

        params.emplace_back(node->param_name, val);
        std::string_view rest = (slash == std::string_view::npos) ? "" : path.substr(slash);

        if (rest.empty() || rest == "/") {
            if (node->has_any_handler) {
                matched_node = node;
                return true;
            }
        }

        for (const auto& c : node->children) {
            if (match_node(c.get(), rest, params, matched_node)) {
                return true;
            }
        }

        params.pop_back(); // Backtrack
        return false;
    }

    if (node->type == RadixNodeType::Wildcard) {
        if (!node->param_name.empty()) {
            params.emplace_back(node->param_name, path);
        }
        if (node->has_any_handler) {
            matched_node = node;
            return true;
        }
        return false;
    }

    return false;
}

RadixTree::MatchResult RadixTree::match(Request& req) const {
    std::string_view path = normalize_path(req.path());

    std::vector<std::pair<std::string_view, std::string_view>> extracted_params;
    const RadixNode* matched_node = nullptr;

    if (!match_node(root_.get(), path, extracted_params, matched_node)) {
        return MatchResult{.handler = nullptr, .route_found = false, .method_not_allowed = false};
    }

    size_t midx = static_cast<size_t>(req.method());
    if (matched_node && midx < matched_node->handlers.size() && matched_node->has_handler[midx]) {
        for (const auto& [k, v] : extracted_params) {
            req.add_param(k, v);
        }
        return MatchResult{
            .handler = &matched_node->handlers[midx],
            .route_found = true,
            .method_not_allowed = false
        };
    }

    // Path matched a registered route, but HTTP method not registered on this route
    return MatchResult{
        .handler = nullptr,
        .route_found = true,
        .method_not_allowed = true
    };
}

} // namespace aegon::http
