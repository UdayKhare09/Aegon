#include "http/client/HttpClient.h"
#include "http/v1/Http1Parser.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace aegon::http::client {

namespace {

inline std::string url_encode(std::string_view value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << static_cast<char>(c);
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(c);
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// RequestState & RequestBuilder Implementation
// -----------------------------------------------------------------------------

RequestState::RequestState(HttpClient& c, Method m, std::string u)
    : client(c), method(m), url(std::move(u)) {}

RequestBuilder::RequestBuilder(HttpClient& client, Method method, std::string url)
    : state_(std::make_shared<RequestState>(client, method, std::move(url))) {
    state_->version = client.config().default_protocol;
    state_->timeout = client.config().timeout;
    state_->follow_redirects = client.config().follow_redirects;
    header("User-Agent", client.config().user_agent);
}

RequestBuilder& RequestBuilder::version(HttpVersion v) {
    state_->version = v;
    return *this;
}

RequestBuilder& RequestBuilder::query(std::string_view key, std::string_view value) {
    char sep = (state_->url.find('?') == std::string::npos) ? '?' : '&';
    state_->url += sep;
    state_->url += url_encode(key);
    state_->url += '=';
    state_->url += url_encode(value);
    return *this;
}

RequestBuilder& RequestBuilder::query(const std::vector<std::pair<std::string, std::string>>& params) {
    for (const auto& [k, v] : params) {
        query(k, v);
    }
    return *this;
}

RequestBuilder& RequestBuilder::query(std::string_view raw_query) {
    if (raw_query.empty()) return *this;
    char sep = (state_->url.find('?') == std::string::npos) ? '?' : '&';
    state_->url += sep;
    if (raw_query.front() == '?' || raw_query.front() == '&') {
        state_->url += raw_query.substr(1);
    } else {
        state_->url += raw_query;
    }
    return *this;
}

RequestBuilder& RequestBuilder::header(std::string_view name, std::string_view value) {
    for (auto& h : state_->headers) {
        if (iequals(h.name, name)) {
            h.value = std::string(value);
            return *this;
        }
    }
    state_->headers.push_back({std::string(name), std::string(value)});
    return *this;
}

RequestBuilder& RequestBuilder::bearer_auth(std::string_view token) {
    return header("Authorization", "Bearer " + std::string(token));
}

RequestBuilder& RequestBuilder::basic_auth(std::string_view username, std::string_view password) {
    std::string creds = std::string(username) + ":" + std::string(password);
    return header("Authorization", "Basic " + jwt::base64_encode(creds));
}

RequestBuilder& RequestBuilder::api_key(std::string_view header_name, std::string_view key) {
    return header(header_name, key);
}

RequestBuilder& RequestBuilder::content_type(std::string_view mime) {
    return header("Content-Type", mime);
}

RequestBuilder& RequestBuilder::accept(std::string_view mime) {
    return header("Accept", mime);
}

RequestBuilder& RequestBuilder::user_agent(std::string_view ua) {
    return header("User-Agent", ua);
}

RequestBuilder& RequestBuilder::cookie(std::string_view name, std::string_view value) {
    state_->cookies.push_back(std::string(name) + "=" + std::string(value));
    return *this;
}

RequestBuilder& RequestBuilder::body(std::string b) {
    state_->body = std::move(b);
    return *this;
}

RequestBuilder& RequestBuilder::body(std::string_view b) {
    state_->body = std::string(b);
    return *this;
}

RequestBuilder& RequestBuilder::body(std::span<const uint8_t> bytes) {
    state_->body = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return *this;
}

RequestBuilder& RequestBuilder::form(const std::vector<std::pair<std::string, std::string>>& fields) {
    header("Content-Type", "application/x-www-form-urlencoded");
    std::string enc;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) enc += '&';
        enc += url_encode(fields[i].first);
        enc += '=';
        enc += url_encode(fields[i].second);
    }
    state_->body = std::move(enc);
    return *this;
}

RequestBuilder& RequestBuilder::timeout(std::chrono::milliseconds ms) {
    state_->timeout = ms;
    return *this;
}

RequestBuilder& RequestBuilder::follow_redirects(bool follow) {
    state_->follow_redirects = follow;
    return *this;
}

RequestBuilder& RequestBuilder::retry(uint8_t count, std::chrono::milliseconds backoff) {
    state_->retries = count;
    state_->retry_backoff = backoff;
    return *this;
}

core::Task<Response> RequestBuilder::send() {
    auto state = state_;
    core::EventLoop* loop = core::EventLoop::current();
    if (!loop) {
        co_return send_sync();
    }
    co_return co_await state->client.execute(state, *loop);
}

static core::Task<void> run_sync_request(std::shared_ptr<RequestState> state,
                                         core::EventLoop* loop,
                                         std::optional<Response>* out_res) {
    *out_res = co_await state->client.execute(state, *loop);
    loop->stop();
}

Response RequestBuilder::send_sync() {
    core::EventLoop temp_loop(256, 128, 4096);
    std::optional<Response> sync_res;

    temp_loop.spawn(run_sync_request(state_, &temp_loop, &sync_res));
    temp_loop.run();

    if (sync_res) {
        return std::move(*sync_res);
    }
    Response err_res;
    err_res.status(StatusCode::InternalServerError).body("Failed to execute request: loop execution error");
    return err_res;
}

// -----------------------------------------------------------------------------
// HttpClient Implementation
// -----------------------------------------------------------------------------

HttpClient::HttpClient(ClientConfig config)
    : config_(std::move(config)),
      pool_(PoolConfig{
          .max_connections_per_host = config_.max_connections_per_host,
          .max_idle_connections = config_.max_idle_connections,
          .idle_timeout = config_.idle_timeout,
          .connect_timeout = config_.connect_timeout
      }, config_.tls) {}

HttpClient::~HttpClient() {
    close();
}

HttpClient::HttpClient(HttpClient&& other) noexcept
    : config_(std::move(other.config_)), pool_(std::move(other.pool_)) {}

HttpClient& HttpClient::operator=(HttpClient&& other) noexcept {
    if (this != &other) {
        close();
        config_ = std::move(other.config_);
        pool_ = std::move(other.pool_);
    }
    return *this;
}

void HttpClient::close() {
    pool_.clear();
}

RequestBuilder HttpClient::get(std::string_view url) {
    return request(Method::GET, url);
}

RequestBuilder HttpClient::post(std::string_view url) {
    return request(Method::POST, url);
}

RequestBuilder HttpClient::put(std::string_view url) {
    return request(Method::PUT, url);
}

RequestBuilder HttpClient::patch(std::string_view url) {
    return request(Method::PATCH, url);
}

RequestBuilder HttpClient::del(std::string_view url) {
    return request(Method::DELETE, url);
}

RequestBuilder HttpClient::head(std::string_view url) {
    return request(Method::HEAD, url);
}

RequestBuilder HttpClient::options(std::string_view url) {
    return request(Method::OPTIONS, url);
}

RequestBuilder HttpClient::request(Method method, std::string_view url) {
    return RequestBuilder(*this, method, std::string(url));
}

core::Task<Response> HttpClient::execute(const RequestBuilder& req, core::EventLoop& loop) {
    co_return co_await execute(req.state(), loop);
}

core::Task<Response> HttpClient::execute(std::shared_ptr<RequestState> state, core::EventLoop& loop) {
    auto url_opt = Url::parse(state->url);
    if (!url_opt) {
        Response bad_res;
        bad_res.status(StatusCode::BadRequest).body("Invalid URL: absolute URL with scheme required");
        co_return bad_res;
    }

    // Assemble HTTP/1.1 wire packet
    std::string wire;
    wire.reserve(512 + state->body.size());

    wire += to_string(state->method);
    wire += " ";
    wire += url_opt->target();
    wire += " ";
    wire += to_string(state->version);
    wire += "\r\n";

    wire += "Host: ";
    wire += url_opt->host_header();
    wire += "\r\n";

    // Add headers from request state
    bool has_connection = false;
    for (const auto& entry : state->headers) {
        if (iequals(entry.name, "host")) continue; // already added
        if (iequals(entry.name, "connection")) has_connection = true;
        wire += entry.name;
        wire += ": ";
        wire += entry.value;
        wire += "\r\n";
    }

    if (!state->cookies.empty()) {
        std::string cookie_str;
        for (size_t i = 0; i < state->cookies.size(); ++i) {
            if (i > 0) cookie_str += "; ";
            cookie_str += state->cookies[i];
        }
        wire += "Cookie: ";
        wire += cookie_str;
        wire += "\r\n";
    }

    if (!has_connection) {
        wire += "Connection: keep-alive\r\n";
    }

    if (!state->body.empty()) {
        wire += "Content-Length: ";
        wire += std::to_string(state->body.size());
        wire += "\r\n";
    }

    wire += "\r\n";
    wire += state->body;

    // Acquire persistent connection from pool
    auto conn_res = co_await pool_.acquire(*url_opt, loop);
    if (!conn_res.has_value()) {
        Response err_res;
        err_res.status(StatusCode::BadGateway).body("Failed to connect to host: " + std::string(url_opt->host()));
        co_return err_res;
    }

    auto conn = std::move(*conn_res);

    // Write outbound request bytes
    int written = co_await conn->write(wire.data(), wire.size());
    if (written <= 0) {
        conn->close();
        Response err_res;
        err_res.status(StatusCode::BadGateway).body("Failed to transmit request bytes to host");
        co_return err_res;
    }

    // Read and parse response
    std::string recv_buf;
    recv_buf.reserve(4096);
    char chunk[4096];
    Response res;
    size_t consumed = 0;

    while (true) {
        int n = co_await conn->read(chunk, sizeof(chunk));
        if (n <= 0) {
            if (!recv_buf.empty()) {
                auto st = v1::Http1Parser::parse_response(recv_buf, res, consumed);
                if (st == v1::ParseStatus::Complete) break;
            }
            conn->close();
            if (recv_buf.empty()) {
                res.status(StatusCode::BadGateway).body("Remote server closed connection unexpectedly");
            }
            break;
        }

        recv_buf.append(chunk, n);
        auto st = v1::Http1Parser::parse_response(recv_buf, res, consumed);
        if (st == v1::ParseStatus::Complete) {
            break;
        } else if (st == v1::ParseStatus::Error) {
            conn->close();
            res.status(StatusCode::BadGateway).body("Failed to parse HTTP/1.1 response");
            co_return res;
        }
    }

    // Release connection back to pool if keep-alive
    bool keep_alive = true;
    auto conn_hdr = res.headers().get("connection");
    if (conn_hdr && iequals(*conn_hdr, "close")) {
        keep_alive = false;
    }
    pool_.release(std::move(conn), keep_alive);

    co_return res;
}

} // namespace aegon::http::client
