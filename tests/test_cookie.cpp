#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "http/Request.h"
#include "http/Response.h"
#include "http/Cookie.h"

using namespace aegon::http;

void test_cookie_formatting() {
    std::cout << "[TEST] test_cookie_formatting..." << std::endl;

    // Full options with designated initializers
    CookieOptions opts{
        .name = "session_id",
        .value = "xyz123",
        .path = "/",
        .domain = "example.com",
        .max_age = std::chrono::seconds(3600),
        .same_site = SameSite::Strict,
        .http_only = true,
        .secure = true,
        .partitioned = true
    };

    std::string formatted = format_cookie(opts);
    assert(formatted == "session_id=xyz123; Path=/; Domain=example.com; Max-Age=3600; SameSite=Strict; Secure; HttpOnly; Partitioned");

    // Minimal options (default Lax, default Path=/)
    CookieOptions min_opts{
        .name = "theme",
        .value = "dark"
    };
    std::string min_formatted = format_cookie(min_opts);
    assert(min_formatted == "theme=dark; Path=/; SameSite=Lax");

    // SameSite::None with Secure
    CookieOptions none_opts{
        .name = "cross_site",
        .value = "val",
        .same_site = SameSite::None,
        .secure = true
    };
    std::string none_formatted = format_cookie(none_opts);
    assert(none_formatted == "cross_site=val; Path=/; SameSite=None; Secure");

    std::cout << "  -> test_cookie_formatting passed!" << std::endl;
}

void test_response_set_cookie() {
    std::cout << "[TEST] test_response_set_cookie..." << std::endl;

    Response res;
    // Set first cookie
    res.set_cookie({
        .name = "session_id",
        .value = "token_abc_123",
        .path = "/",
        .max_age = std::chrono::hours(2),
        .same_site = SameSite::Strict,
        .http_only = true,
        .secure = true
    });

    // Set second cookie on the same response
    res.set_cookie({
        .name = "user_pref",
        .value = "dark_mode",
        .path = "/app",
        .same_site = SameSite::Lax
    });

    // Verify both Set-Cookie headers exist in HeaderMap
    std::vector<std::string_view> set_cookies;
    for (const auto& h : res.headers()) {
        if (iequals(h.name, "Set-Cookie")) {
            set_cookies.push_back(h.value);
        }
    }
    assert(set_cookies.size() == 2);
    assert(set_cookies[0] == "session_id=token_abc_123; Path=/; Max-Age=7200; SameSite=Strict; Secure; HttpOnly");
    assert(set_cookies[1] == "user_pref=dark_mode; Path=/app; SameSite=Lax");

    // Verify serialized HTTP response
    std::string serialized;
    res.serialize_http1(serialized);
    assert(serialized.find("Set-Cookie: session_id=token_abc_123; Path=/; Max-Age=7200; SameSite=Strict; Secure; HttpOnly\r\n") != std::string::npos);
    assert(serialized.find("Set-Cookie: user_pref=dark_mode; Path=/app; SameSite=Lax\r\n") != std::string::npos);

    std::cout << "  -> test_response_set_cookie passed!" << std::endl;
}

void test_response_clear_cookie() {
    std::cout << "[TEST] test_response_clear_cookie..." << std::endl;

    Response res;
    res.clear_cookie("session_id", "/");

    std::vector<std::string_view> set_cookies;
    for (const auto& h : res.headers()) {
        if (iequals(h.name, "Set-Cookie")) {
            set_cookies.push_back(h.value);
        }
    }
    assert(set_cookies.size() == 1);
    assert(set_cookies[0] == "session_id=; Path=/; Max-Age=0; SameSite=Lax");

    std::cout << "  -> test_response_clear_cookie passed!" << std::endl;
}

void test_request_cookie_parsing() {
    std::cout << "[TEST] test_request_cookie_parsing..." << std::endl;

    Request req;

    // No Cookie header
    assert(!req.cookie("session_id").has_value());

    // Single cookie
    req.headers().set("Cookie", "session_id=abc123xyz");
    auto c1 = req.cookie("session_id");
    assert(c1.has_value());
    assert(*c1 == "abc123xyz");
    assert(!req.cookie("missing").has_value());

    // Multiple cookies with spaces
    req.headers().set("Cookie", "session_id=abc123xyz; theme=dark; auth_token=secret456");
    assert(req.cookie("session_id") == "abc123xyz");
    assert(req.cookie("theme") == "dark");
    assert(req.cookie("auth_token") == "secret456");
    assert(!req.cookie("unknown").has_value());

    // Quoted cookie value per RFC 6265
    req.headers().set("Cookie", "session_id=\"quoted_val_789\"; other=1");
    assert(req.cookie("session_id") == "quoted_val_789");
    assert(req.cookie("other") == "1");

    // Extra whitespace between key and value
    req.headers().set("Cookie", "  token = xyz_space ; pref = light ");
    assert(req.cookie("token") == "xyz_space");
    assert(req.cookie("pref") == "light");

    std::cout << "  -> test_request_cookie_parsing passed!" << std::endl;
}

int main() {
    std::cout << "=== Aegon HTTP Cookie Unit Tests ===" << std::endl;
    test_cookie_formatting();
    test_response_set_cookie();
    test_response_clear_cookie();
    test_request_cookie_parsing();
    std::cout << "All Cookie tests passed successfully!" << std::endl;
    return 0;
}
