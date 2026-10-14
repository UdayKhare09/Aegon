#include "core/simd/SimdString.h"
#include <cassert>
#include <iostream>
#include <string>

using aegon::core::simd::SimdString;

void test_find_char() {
    std::string s = "GET /health HTTP/1.1\r\nHost: 127.0.0.1:8000\r\nUser-Agent: wrk\r\nAccept: */*\r\n\r\n";
    assert(SimdString::find_char(s, ' ') == 3);
    assert(SimdString::find_char(s, ' ', 4) == 11);
    assert(SimdString::find_char(s, ':') == 26);
    assert(SimdString::find_char(s, 'Z') == std::string_view::npos);
    assert(SimdString::find_char("", 'a') == std::string_view::npos);

    // Test long buffer > 128 bytes
    std::string long_s(200, 'a');
    long_s[150] = 'b';
    assert(SimdString::find_char(long_s, 'b') == 150);
    assert(SimdString::find_char(long_s, 'c') == std::string_view::npos);
}

void test_find_crlf() {
    std::string s = "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    assert(SimdString::find_crlf(s) == 20);
    assert(SimdString::find_crlf(s, 22) == 37);
    assert(SimdString::find_crlf(s, 39) == 39);
    assert(SimdString::find_crlf("hello", 0) == std::string_view::npos);
    assert(SimdString::find_crlf("\r", 0) == std::string_view::npos);
    assert(SimdString::find_crlf("", 0) == std::string_view::npos);
    assert(SimdString::find_crlf("hello\r\n", std::string_view::npos) == std::string_view::npos);
    assert(SimdString::find_crlf("hello\r\n", 100) == std::string_view::npos);

    // Test pattern across 64-byte boundary
    std::string long_s(150, 'x');
    long_s[63] = '\r';
    long_s[64] = '\n';
    assert(SimdString::find_crlf(long_s) == 63);

    long_s[127] = '\r';
    long_s[128] = '\n';
    assert(SimdString::find_crlf(long_s, 65) == 127);
}

void test_find_double_crlf() {
    std::string s = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\nBody";
    assert(SimdString::find_double_crlf(s) == 37);
    assert(SimdString::find_double_crlf("invalid headers\r\n") == std::string_view::npos);
    assert(SimdString::find_double_crlf("invalid headers\r\n\r\n", std::string_view::npos) == std::string_view::npos);
    assert(SimdString::find_double_crlf("invalid headers\r\n\r\n", 500) == std::string_view::npos);

    std::string long_s(200, 'w');
    long_s[130] = '\r';
    long_s[131] = '\n';
    long_s[132] = '\r';
    long_s[133] = '\n';
    assert(SimdString::find_double_crlf(long_s) == 130);
}

void test_iequals() {
    assert(SimdString::iequals("", ""));
    assert(!SimdString::iequals("a", ""));
    assert(!SimdString::iequals("", "a"));
    assert(SimdString::iequals("Host", "host"));
    assert(SimdString::iequals("HOST", "host"));
    assert(SimdString::iequals("Content-Length", "content-length"));
    assert(SimdString::iequals("Transfer-Encoding", "transfer-encoding"));
    assert(!SimdString::iequals("Content-Length", "content-type"));
    assert(!SimdString::iequals("Host", "Accept"));
    assert(SimdString::iequals("1234567890abcdef", "1234567890ABCDEF"));
    assert(SimdString::iequals("1234567890abcdef1234567890abcdef", "1234567890ABCDEF1234567890ABCDEF"));
}

void test_has_url_encoding_chars() {
    assert(!SimdString::has_url_encoding_chars(""));
    assert(!SimdString::has_url_encoding_chars("hello_world_12345"));
    assert(SimdString::has_url_encoding_chars("%20"));
    assert(SimdString::has_url_encoding_chars("foo+bar"));
    assert(SimdString::has_url_encoding_chars("key=%2Fpath%2Fto%2Ffile"));
    std::string long_no_enc(120, 'a');
    assert(!SimdString::has_url_encoding_chars(long_no_enc));
    long_no_enc[75] = '%';
    assert(SimdString::has_url_encoding_chars(long_no_enc));
    long_no_enc[75] = '+';
    assert(SimdString::has_url_encoding_chars(long_no_enc));
}

void test_validate_uri_chars() {
    assert(SimdString::validate_uri_chars(""));
    assert(SimdString::validate_uri_chars("/"));
    assert(SimdString::validate_uri_chars("/api/v1/users?name=john&age=30"));
    assert(!SimdString::validate_uri_chars("/api/v1/users with space"));
    assert(!SimdString::validate_uri_chars("/api/v1/users\twith_tab"));
    assert(!SimdString::validate_uri_chars("/api/v1/users\nwith_newline"));
    assert(!SimdString::validate_uri_chars(std::string_view("/api\0test", 9)));

    // Long URI across 32-byte boundary
    std::string long_uri = "/this/is/a/very/long/uri/path/that/exceeds/thirty/two/bytes/for/avx2/testing";
    assert(SimdString::validate_uri_chars(long_uri));
    long_uri[45] = ' ';
    assert(!SimdString::validate_uri_chars(long_uri));
    long_uri[45] = 0x7f; // DEL
    assert(!SimdString::validate_uri_chars(long_uri));
}

void test_validate_tchars() {
    assert(SimdString::validate_tchars(""));
    assert(SimdString::validate_tchars("GET"));
    assert(SimdString::validate_tchars("content-type"));
    assert(SimdString::validate_tchars("Transfer-Encoding"));
    assert(SimdString::validate_tchars("!#$%&'*+-.^_`|~"));
    assert(!SimdString::validate_tchars("content:type"));
    assert(!SimdString::validate_tchars("content length"));
    assert(!SimdString::validate_tchars("header@name"));
    assert(!SimdString::validate_tchars("header/name"));
    assert(!SimdString::validate_tchars("header?name"));

    std::string long_token(64, 'a');
    assert(SimdString::validate_tchars(long_token));
    long_token[50] = ' ';
    assert(!SimdString::validate_tchars(long_token));
}

void test_validate_header_value() {
    assert(SimdString::validate_header_value(""));
    assert(SimdString::validate_header_value("application/json"));
    assert(SimdString::validate_header_value("text/html; charset=UTF-8"));
    assert(SimdString::validate_header_value("val\twith\ttab"));
    assert(!SimdString::validate_header_value("val\rwith_cr"));
    assert(!SimdString::validate_header_value("val\nwith_lf"));
    assert(!SimdString::validate_header_value(std::string_view("val\0with_nul", 12)));
    assert(!SimdString::validate_header_value("val\x1f" "ctl"));
    assert(!SimdString::validate_header_value("val\x7f" "del"));

    std::string long_val(80, 'x');
    assert(SimdString::validate_header_value(long_val));
    long_val[35] = '\r';
    assert(!SimdString::validate_header_value(long_val));
    long_val[35] = '\x01';
    assert(!SimdString::validate_header_value(long_val));
}

void test_validate_h2_h3_header_value() {
    assert(SimdString::validate_h2_h3_header_value(""));
    assert(SimdString::validate_h2_h3_header_value("gzip, deflate, br"));
    assert(SimdString::validate_h2_h3_header_value("binary\x01\x02\xff" "data"));
    assert(!SimdString::validate_h2_h3_header_value(std::string_view("has\0nul", 7)));
    assert(!SimdString::validate_h2_h3_header_value("has\rcr"));
    assert(!SimdString::validate_h2_h3_header_value("has\nlf"));

    std::string long_h2_val(100, 'z');
    assert(SimdString::validate_h2_h3_header_value(long_h2_val));
    long_h2_val[67] = '\n';
    assert(!SimdString::validate_h2_h3_header_value(long_h2_val));
}

int main() {
    test_find_char();
    test_find_crlf();
    test_find_double_crlf();
    test_iequals();
    test_has_url_encoding_chars();
    test_validate_uri_chars();
    test_validate_tchars();
    test_validate_header_value();
    test_validate_h2_h3_header_value();
    std::cout << "All SIMD String tests passed successfully!\n";
    return 0;
}
