#include "http/v2/Http2Connection.h"
#include "core/simd/SimdString.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <iostream>

namespace aegon::http::v2 {

namespace {

int on_header_cb(nghttp2_session*, const nghttp2_frame* frame,
                 const uint8_t* name, size_t namelen,
                 const uint8_t* value, size_t valuelen,
                 uint8_t flags, void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_header(
        frame, name, namelen, value, valuelen, flags);
}

int on_data_chunk_recv_cb(nghttp2_session*, uint8_t flags,
                          int32_t stream_id, const uint8_t* data,
                          size_t len, void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_data_chunk_recv(
        flags, stream_id, data, len);
}

int on_frame_recv_cb(nghttp2_session*, const nghttp2_frame* frame, void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_frame_recv(frame);
}

int on_frame_send_cb(nghttp2_session*, const nghttp2_frame* frame, void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_frame_send(frame);
}

int on_invalid_frame_recv_cb(nghttp2_session*, const nghttp2_frame* frame, int lib_error_code, void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_invalid_frame_recv(frame, lib_error_code);
}

int on_stream_close_cb(nghttp2_session*, int32_t stream_id, uint32_t error_code, void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_stream_close(stream_id, error_code);
}

ssize_t data_source_read_cb(nghttp2_session*, int32_t stream_id,
                            uint8_t* buf, size_t length,
                            uint32_t* data_flags,
                            nghttp2_data_source*,
                            void* user_data) {
    return static_cast<Http2Connection*>(user_data)->on_data_source_read(
        stream_id, buf, length, data_flags);
}

} // anonymous namespace

Http2Connection::Http2Connection(core::EventLoop& loop, int client_fd, const Router& router, 
                                 const ServiceRegistry* services, OutputSender sender)
    : loop_(loop), client_fd_(client_fd), router_(router), services_(services), sender_(std::move(sender)) {
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);

    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_cb);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_cb);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_cb);
    nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, on_frame_send_cb);
    nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks, on_invalid_frame_recv_cb);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_cb);

    nghttp2_session_server_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
}

Http2Connection::~Http2Connection() {
    if (session_) {
        nghttp2_session_del(session_);
        session_ = nullptr;
    }
}

Http2Stream* Http2Connection::get_or_create_stream(int32_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        auto stream = std::make_unique<Http2Stream>();
        stream->stream_id = stream_id;
        stream->req.set_version(HttpVersion::Http2);
        auto* ptr = stream.get();
        streams_.emplace(stream_id, std::move(stream));
        return ptr;
    }
    return it->second.get();
}

int Http2Connection::on_header(const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
                               const uint8_t* value, size_t valuelen, uint8_t) {
    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
        return 0;
    }

    int32_t sid = frame->hd.stream_id;
    if (closed_stream_ids_.contains(sid)) {
        nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, sid, NGHTTP2_STREAM_CLOSED);
        return 0;
    }

    if (streams_.find(sid) == streams_.end()) {
        if (sid <= max_remote_stream_id_) {
            nghttp2_session_terminate_session(session_, NGHTTP2_PROTOCOL_ERROR);
            closed_ = true;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        max_remote_stream_id_ = sid;
    }

    auto* stream = get_or_create_stream(sid);
    if (stream->request_complete) {
        stream->reset = true;
        nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, sid, NGHTTP2_STREAM_CLOSED);
        return 0;
    }

    std::string_view n(reinterpret_cast<const char*>(name), namelen);
    std::string_view v(reinterpret_cast<const char*>(value), valuelen);

    stream->headers_total_size += namelen + valuelen;
    if (stream->headers_total_size > MAX_HEADERS_SIZE && stream->error_status == StatusCode::Ok) {
        stream->error_status = StatusCode::RequestHeaderFieldsTooLarge;
    }

    if (n == ":method") {
        stream->req.set_method(string_to_method(v));
    } else if (n == ":path") {
        if (v.size() > MAX_URI_LENGTH && stream->error_status == StatusCode::Ok) {
            stream->error_status = StatusCode::UriTooLong;
        }
        size_t qmark = core::simd::SimdString::find_char(v, '?');
        if (qmark != std::string_view::npos) {
            stream->path_storage.assign(v.data(), qmark);
            stream->query_storage.assign(v.data() + qmark + 1, v.size() - qmark - 1);
            stream->req.set_path(stream->path_storage);
            stream->req.set_query(stream->query_storage);
        } else {
            stream->path_storage.assign(v.data(), v.size());
            stream->query_storage.clear();
            stream->req.set_path(stream->path_storage);
            stream->req.set_query("");
        }
    } else if (n == ":authority") {
        stream->header_storage.emplace_back("Host", std::string(v));
        const auto& back = stream->header_storage.back();
        stream->req.headers().add(back.first, back.second);
    } else if (n.starts_with(':')) {
        // Other pseudo headers (:scheme, etc.)
    } else {
        stream->header_storage.emplace_back(std::string(n), std::string(v));
        const auto& back = stream->header_storage.back();
        stream->req.headers().add(back.first, back.second);
    }

    return 0;
}

int Http2Connection::on_data_chunk_recv(uint8_t, int32_t stream_id, const uint8_t* data, size_t len) {
    if (closed_stream_ids_.contains(stream_id)) {
        nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, stream_id, NGHTTP2_STREAM_CLOSED);
        return 0;
    }
    auto* stream = get_or_create_stream(stream_id);
    if (stream->request_complete) {
        stream->reset = true;
        nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, stream_id, NGHTTP2_STREAM_CLOSED);
        return 0;
    }
    if (stream->body_accum.size() + len > MAX_BODY_SIZE) {
        if (stream->error_status == StatusCode::Ok) {
            stream->error_status = StatusCode::PayloadTooLarge;
        }
        return 0;
    }
    stream->body_accum.append(reinterpret_cast<const char*>(data), len);
    return 0;
}

int Http2Connection::on_frame_recv(const nghttp2_frame* frame) {
    if (frame->hd.type == NGHTTP2_HEADERS) {
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            auto* stream = get_or_create_stream(frame->hd.stream_id);
            stream->request_complete = true;
            pending_dispatch_.push_back(frame->hd.stream_id);
        }
    } else if (frame->hd.type == NGHTTP2_DATA) {
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            auto* stream = get_or_create_stream(frame->hd.stream_id);
            stream->request_complete = true;
            pending_dispatch_.push_back(frame->hd.stream_id);
        }
    } else if (frame->hd.type == NGHTTP2_PRIORITY) {
        if (frame->hd.stream_id == 0) {
            nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE, 0, NGHTTP2_PROTOCOL_ERROR, nullptr, 0);
            closed_ = true;
        } else if (frame->priority.pri_spec.stream_id == frame->hd.stream_id) {
            nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, frame->hd.stream_id, NGHTTP2_PROTOCOL_ERROR);
        }
    } else if (frame->hd.type == NGHTTP2_RST_STREAM) {
        // CVE-2023-44487: Rapid Reset Attack mitigation
        ++rst_count_;
        if (rst_count_ > rst_burst_limit_) {
            nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE, frame->hd.stream_id,
                                  NGHTTP2_ENHANCE_YOUR_CALM, nullptr, 0);
            closed_ = true;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    } else if (frame->hd.type == NGHTTP2_GOAWAY) {
        closed_ = true;
    }
    return 0;
}

int Http2Connection::on_frame_send(const nghttp2_frame* frame) {
    if (frame->hd.type == NGHTTP2_GOAWAY) {
        closed_ = true;
    }
    return 0;
}

int Http2Connection::on_invalid_frame_recv(const nghttp2_frame* frame, int lib_error_code) {
    if (frame->hd.stream_id != 0) {
        auto it = streams_.find(frame->hd.stream_id);
        if (it != streams_.end()) {
            it->second->reset = true;
        }
        if (lib_error_code == NGHTTP2_ERR_STREAM_CLOSED || lib_error_code == NGHTTP2_ERR_STREAM_CLOSING) {
            nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, frame->hd.stream_id, NGHTTP2_STREAM_CLOSED);
        } else {
            nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, frame->hd.stream_id, NGHTTP2_PROTOCOL_ERROR);
        }
    } else {
        nghttp2_session_terminate_session(session_, NGHTTP2_PROTOCOL_ERROR);
        closed_ = true;
    }
    return 0;
}

int Http2Connection::on_stream_close(int32_t stream_id, uint32_t) {
    closed_stream_ids_.insert(stream_id);
    if (closed_stream_ids_.size() > 1024) {
        closed_stream_ids_.erase(closed_stream_ids_.begin());
    }
    streams_.erase(stream_id);
    return 0;
}

ssize_t Http2Connection::on_data_source_read(int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    }

    auto* stream = it->second.get();
    std::string_view body = stream->res.body();
    size_t available = (stream->body_offset < body.size()) ? (body.size() - stream->body_offset) : 0;
    size_t to_copy = std::min(available, length);

    if (to_copy > 0) {
        std::memcpy(buf, body.data() + stream->body_offset, to_copy);
        stream->body_offset += to_copy;
    }

    if (stream->body_offset >= body.size()) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }

    return static_cast<ssize_t>(to_copy);
}

void Http2Connection::submit_response(Http2Stream* stream) {
    char status_buf[16];
    auto [p_status, _s] = std::to_chars(status_buf, status_buf + sizeof(status_buf), static_cast<uint16_t>(stream->res.status()));
    size_t status_len = static_cast<size_t>(p_status - status_buf);

    char cl_buf[32];
    auto [p_cl, _c] = std::to_chars(cl_buf, cl_buf + sizeof(cl_buf), stream->res.body().size());
    size_t cl_len = static_cast<size_t>(p_cl - cl_buf);

    std::array<nghttp2_nv, 16> nva_stack;
    std::vector<nghttp2_nv> nva_heap;
    nghttp2_nv* nva_ptr = nva_stack.data();
    size_t nva_count = 0;

    auto push_nv = [&](const uint8_t* name, size_t namelen, const uint8_t* val, size_t vallen) {
        nghttp2_nv nv{
            .name = const_cast<uint8_t*>(name),
            .value = const_cast<uint8_t*>(val),
            .namelen = namelen,
            .valuelen = vallen,
            .flags = NGHTTP2_NV_FLAG_NONE
        };
        if (nva_count < nva_stack.size() && nva_heap.empty()) {
            nva_stack[nva_count++] = nv;
        } else {
            if (nva_heap.empty()) {
                nva_heap.reserve(16 + stream->res.headers().size());
                for (size_t i = 0; i < nva_count; ++i) {
                    nva_heap.push_back(nva_stack[i]);
                }
            }
            nva_heap.push_back(nv);
            nva_count = nva_heap.size();
            nva_ptr = nva_heap.data();
        }
    };

    push_nv(reinterpret_cast<const uint8_t*>(":status"), 7,
            reinterpret_cast<const uint8_t*>(status_buf), status_len);

    if (!should_suppress_content_length(stream->res.status()) && !stream->res.headers().contains("content-length")) {
        push_nv(reinterpret_cast<const uint8_t*>("content-length"), 14,
                reinterpret_cast<const uint8_t*>(cl_buf), cl_len);
    }

    // Keep lowercased header names alive for nghttp2_nv pointers
    std::vector<std::string> lower_names;
    lower_names.reserve(stream->res.headers().size());

    for (const auto& h : stream->res.headers()) {
        if (is_hop_by_hop_header(h.name)) {
            continue;
        }
        lower_names.push_back(to_lower_ascii(h.name));
        const auto& ln = lower_names.back();
        push_nv(reinterpret_cast<const uint8_t*>(ln.data()), ln.size(),
                reinterpret_cast<const uint8_t*>(h.value.data()), h.value.size());
    }

    if (!alt_svc_.empty() && !stream->res.headers().contains("alt-svc")) {
        push_nv(reinterpret_cast<const uint8_t*>("alt-svc"), 7,
                reinterpret_cast<const uint8_t*>(alt_svc_.data()), alt_svc_.size());
    }

    nghttp2_data_provider prd;
    prd.source.ptr = stream;
    prd.read_callback = data_source_read_cb;

    nghttp2_submit_response(session_, stream->stream_id, nva_ptr, nva_count, &prd);
    stream->response_submitted = true;
}

core::Task<bool> Http2Connection::init() {
    nghttp2_settings_entry iv[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 256},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 1048576}
    };
    int rv = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv, 2);
    if (rv != 0) {
        co_return false;
    }
    co_return co_await flush_outbound();
}

namespace {
std::vector<uint8_t> base64url_decode(std::string_view in) {
    std::string s(in);
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4 != 0) {
        s.push_back('=');
    }
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    for (size_t i = 0; i + 3 < s.size(); i += 4) {
        int a = decode_char(s[i]);
        int b = decode_char(s[i + 1]);
        int c = (s[i + 2] == '=') ? 0 : decode_char(s[i + 2]);
        int d = (s[i + 3] == '=') ? 0 : decode_char(s[i + 3]);
        if (a < 0 || b < 0) break;
        out.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
        if (s[i + 2] != '=') {
            out.push_back(static_cast<uint8_t>(((b & 0xf) << 4) | (c >> 2)));
        }
        if (s[i + 3] != '=') {
            out.push_back(static_cast<uint8_t>(((c & 0x3) << 6) | d));
        }
    }
    return out;
}
} // anonymous namespace

core::Task<bool> Http2Connection::upgrade_request(Request req, std::string_view http2_settings) {
    auto settings_bin = base64url_decode(http2_settings);
    int rv = nghttp2_session_upgrade2(session_, settings_bin.data(), settings_bin.size(),
                                      req.method() == Method::HEAD ? 1 : 0, nullptr);
    if (rv != 0) {
        co_return false;
    }

    auto* stream = get_or_create_stream(1);
    stream->req = std::move(req);

    co_await router_.dispatch(stream->req, stream->res, services_);

    submit_response(stream);
    co_return co_await flush_outbound();
}

core::Task<void> Http2Connection::dispatch_pending_requests() {
    if (pending_dispatch_.empty()) co_return;

    auto ready = std::move(pending_dispatch_);
    pending_dispatch_.clear();

    for (int32_t sid : ready) {
        if (closed_stream_ids_.contains(sid)) continue;
        auto it = streams_.find(sid);
        if (it == streams_.end()) continue;
        auto* stream = it->second.get();
        if (stream->reset || stream->response_submitted) continue;

        if (stream->error_status != StatusCode::Ok) {
            stream->res.status(stream->error_status).text(status_phrase(stream->error_status));
            submit_response(stream);
            continue;
        }

        if (!stream->body_accum.empty()) {
            stream->req.set_body(stream->body_accum);
        }

        co_await router_.dispatch(stream->req, stream->res, services_);

        submit_response(stream);
    }
}

core::Task<bool> Http2Connection::flush_outbound() {
    outbound_buf_.clear();
    while (nghttp2_session_want_write(session_)) {
        const uint8_t* data = nullptr;
        ssize_t len = nghttp2_session_mem_send(session_, &data);
        if (len < 0) {
            closed_ = true;
            co_return false;
        }
        if (len == 0 || data == nullptr) {
            break;
        }

        outbound_buf_.append(reinterpret_cast<const char*>(data), static_cast<size_t>(len));

        if (outbound_buf_.size() >= 65536) {
            int sent = 0;
            if (sender_) {
                sent = co_await sender_(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(outbound_buf_.data()), outbound_buf_.size()));
            } else {
                sent = co_await loop_.ring().send_all(client_fd_, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(outbound_buf_.data()), outbound_buf_.size()));
            }
            if (sent <= 0) {
                closed_ = true;
                co_return false;
            }
            outbound_buf_.clear();
        }
    }

    if (!outbound_buf_.empty()) {
        int sent = 0;
        if (sender_) {
            sent = co_await sender_(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(outbound_buf_.data()), outbound_buf_.size()));
        } else {
            sent = co_await loop_.ring().send_all(client_fd_, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(outbound_buf_.data()), outbound_buf_.size()));
        }
        if (sent <= 0) {
            closed_ = true;
            co_return false;
        }
        outbound_buf_.clear();
    }
    co_return true;
}

core::Task<bool> Http2Connection::feed_data(const void* data, size_t len) {
    ssize_t readlen = nghttp2_session_mem_recv(session_, reinterpret_cast<const uint8_t*>(data), len);
    if (readlen < 0) {
        closed_ = true;
        (void)co_await flush_outbound();
        co_return false;
    }

    co_await dispatch_pending_requests();
    bool flushed = co_await flush_outbound();
    if (!nghttp2_session_want_read(session_) && !nghttp2_session_want_write(session_)) {
        closed_ = true;
    }
    co_return flushed && !closed_;
}

bool Http2Connection::wants_read() const noexcept {
    return !closed_ && nghttp2_session_want_read(session_);
}

bool Http2Connection::wants_write() const noexcept {
    return !closed_ && nghttp2_session_want_write(session_);
}

bool Http2Connection::is_closed() const noexcept {
    return closed_;
}

} // namespace aegon::http::v2
