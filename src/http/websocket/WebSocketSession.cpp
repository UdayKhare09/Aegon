#include "http/websocket/WebSocketSession.h"
#include <array>

namespace aegon::http::websocket {

WebSocketSession::WebSocketSession(IWebSocketTransport& transport, std::string path,
                                   WebSocketHandler handler,
                                   WebSocketEchoHandler echo_handler,
                                   const WebSocketConfig& config)
    : transport_(transport),
      ws_(transport, std::move(path)),
      handler_(std::move(handler)),
      echo_handler_(std::move(echo_handler)),
      config_(config)
{
    stream_buf_.reserve(config_.initial_buffer_capacity);
    batch_out_.reserve(config_.initial_buffer_capacity);
}

core::Task<bool> WebSocketSession::process_incoming_data(std::string_view data) {
    if (is_closed_) co_return false;

    if (!initialized_) {
        initialized_ = true;
        if (handler_) {
            bool handler_err = false;
            try {
                co_await handler_(ws_);
            } catch (...) {
                handler_err = true;
            }
            if (handler_err) {
                is_closed_ = true;
                ws_.mark_closed();
                co_await transport_.close_transport();
                co_return false;
            }
        }
    }

    std::string_view input_source;
    if (stream_buf_.empty()) {
        input_source = data;
    } else {
        if (!data.empty()) {
            stream_buf_.append(data);
        }
        input_source = stream_buf_;
    }

    batch_out_.clear();
    size_t consumed = 0;

    while (consumed < input_source.size()) {
        std::string_view remaining(input_source.data() + consumed, input_source.size() - consumed);
        FrameHeader header{};
        auto res = parse_frame_header(remaining, header);

        if (res == FrameParseResult::NeedMoreData) {
            break;
        }

        if (res == FrameParseResult::ProtocolError) {
            if (!batch_out_.empty()) {
                (void)co_await transport_.write_raw(batch_out_);
                batch_out_.clear();
            }
            is_closed_ = true;
            co_await ws_.close(CloseCode::ProtocolError, "Protocol Error");
            co_return false;
        }

        // RFC 6455 §7.4.1 / §5.2 Payload and Message Size Enforcement -> CloseCode::MessageTooBig (1009)
        if (header.payload_len > config_.max_frame_size || header.payload_len > config_.max_message_size) {
            is_closed_ = true;
            co_await ws_.close(CloseCode::MessageTooBig, "Message Too Big");
            co_return false;
        }

        // Check if full frame payload is available
        size_t total_frame_len = header.header_len + header.payload_len;
        if (remaining.size() < total_frame_len) {
            break;
        }

        // In-place payload unmasking directly in the ingest buffer
        uint8_t* payload_ptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(input_source.data() + consumed + header.header_len));
        if (header.masked) {
            unmask_payload_inplace(payload_ptr, header.payload_len, header.mask_key);
        } else if (config_.require_masked_frames && transport_.protocol() == TransportProtocol::Http1) {
            // RFC 6455 §5.1: Client-to-server frames MUST be masked on TCP/HTTP1
            is_closed_ = true;
            co_await ws_.close(CloseCode::ProtocolError, "Unmasked client frame");
            co_return false;
        }

        std::string_view payload(reinterpret_cast<const char*>(payload_ptr), header.payload_len);

        if (header.opcode == Opcode::Ping) {
            if (!batch_out_.empty()) {
                (void)co_await transport_.write_raw(batch_out_);
                batch_out_.clear();
            }
            co_await ws_.send_pong(payload);
            if (ws_.ping_callback()) {
                co_await ws_.ping_callback()(ws_, payload);
            }
        } else if (header.opcode == Opcode::Pong) {
            // Pong frame received
        } else if (header.opcode == Opcode::Close) {
            if (payload.size() == 1) {
                batch_out_.clear();
                is_closed_ = true;
                co_await ws_.close(CloseCode::ProtocolError, "Close frame payload cannot be 1 byte");
                co_return false;
            }

            CloseCode code = CloseCode::Normal;
            std::string_view reason;
            if (payload.size() >= 2) {
                uint16_t raw_code;
                std::memcpy(&raw_code, payload.data(), 2);
                uint16_t ucode = be16toh(raw_code);
                if (!is_valid_close_code(ucode)) {
                    batch_out_.clear();
                    is_closed_ = true;
                    co_await ws_.close(CloseCode::ProtocolError, "Invalid close code");
                    co_return false;
                }
                code = static_cast<CloseCode>(ucode);
                if (payload.size() > 2) {
                    reason = payload.substr(2);
                    if (!is_valid_utf8(reason)) {
                        batch_out_.clear();
                        is_closed_ = true;
                        co_await ws_.close(CloseCode::InvalidFramePayload, "Invalid UTF-8 in close reason");
                        co_return false;
                    }
                }
            }

            if (ws_.close_callback()) {
                co_await ws_.close_callback()(ws_, code, reason);
            }

            if (!batch_out_.empty()) {
                (void)co_await transport_.write_raw(batch_out_);
                batch_out_.clear();
            }

            co_await ws_.close(code, reason);
            is_closed_ = true;
            co_return false;
        } else if (header.opcode == Opcode::Continuation) {
            if (!fragment_in_progress_) {
                if (!batch_out_.empty()) {
                    (void)co_await transport_.write_raw(batch_out_);
                    batch_out_.clear();
                }
                is_closed_ = true;
                co_await ws_.close(CloseCode::ProtocolError, "Unexpected continuation frame");
                co_return false;
            }

            if (fragment_buffer_.size() + payload.size() > config_.max_message_size) {
                batch_out_.clear();
                is_closed_ = true;
                co_await ws_.close(CloseCode::MessageTooBig, "Message Too Big");
                co_return false;
            }

            if (fragment_opcode_ == Opcode::Text) {
                if (!utf8_validator_.update(payload)) {
                    batch_out_.clear();
                    is_closed_ = true;
                    co_await ws_.close(CloseCode::InvalidFramePayload, "Invalid UTF-8 in continuation frame");
                    co_return false;
                }
            }

            fragment_buffer_.append(payload);

            if (header.fin) {
                if (fragment_opcode_ == Opcode::Text && !utf8_validator_.is_valid_end()) {
                    batch_out_.clear();
                    is_closed_ = true;
                    co_await ws_.close(CloseCode::InvalidFramePayload, "Incomplete UTF-8 sequence at end of message");
                    co_return false;
                }

                fragment_in_progress_ = false;
                Opcode eff_opcode = fragment_opcode_;
                std::string_view full_payload(fragment_buffer_);

                if (echo_handler_) {
                    co_await echo_handler_(ws_, Message(full_payload, eff_opcode));
                } else if (!ws_.message_callback() && !ws_.text_callback() && !ws_.binary_callback()) {
                    std::array<uint8_t, 10> out_hdr{};
                    size_t hlen = serialize_frame_header(eff_opcode, full_payload.size(), out_hdr.data(), true);
                    batch_out_.append(reinterpret_cast<const char*>(out_hdr.data()), hlen);
                    batch_out_.append(full_payload);
                } else {
                    if (ws_.message_callback()) {
                        co_await ws_.message_callback()(ws_, Message(full_payload, eff_opcode));
                    }
                    if (eff_opcode == Opcode::Text && ws_.text_callback()) {
                        co_await ws_.text_callback()(ws_, full_payload);
                    } else if (eff_opcode == Opcode::Binary && ws_.binary_callback()) {
                        co_await ws_.binary_callback()(ws_, std::span<const uint8_t>(
                            reinterpret_cast<const uint8_t*>(full_payload.data()), full_payload.size()));
                    }
                }
                fragment_buffer_.clear();
            }
        } else if (header.opcode == Opcode::Text || header.opcode == Opcode::Binary) {
            if (fragment_in_progress_) {
                if (!batch_out_.empty()) {
                    (void)co_await transport_.write_raw(batch_out_);
                    batch_out_.clear();
                }
                is_closed_ = true;
                co_await ws_.close(CloseCode::ProtocolError, "Received new data frame while previous fragment incomplete");
                co_return false;
            }

            if (!header.fin) {
                fragment_in_progress_ = true;
                fragment_opcode_ = header.opcode;
                fragment_buffer_.clear();

                if (header.opcode == Opcode::Text) {
                    utf8_validator_.reset();
                    if (!utf8_validator_.update(payload)) {
                        batch_out_.clear();
                        is_closed_ = true;
                        co_await ws_.close(CloseCode::InvalidFramePayload, "Invalid UTF-8 in initial fragment");
                        co_return false;
                    }
                }

                if (payload.size() > config_.max_message_size) {
                    batch_out_.clear();
                    is_closed_ = true;
                    co_await ws_.close(CloseCode::MessageTooBig, "Message Too Big");
                    co_return false;
                }

                fragment_buffer_.assign(payload);
            } else {
                if (header.opcode == Opcode::Text) {
                    utf8_validator_.reset();
                    if (!utf8_validator_.update(payload) || !utf8_validator_.is_valid_end()) {
                        batch_out_.clear();
                        is_closed_ = true;
                        co_await ws_.close(CloseCode::InvalidFramePayload, "Invalid UTF-8 in text frame");
                        co_return false;
                    }
                }

                if (echo_handler_) {
                    co_await echo_handler_(ws_, Message(payload, header.opcode));
                } else if (!ws_.message_callback() && !ws_.text_callback() && !ws_.binary_callback()) {
                    // High-performance fast-path echo batching into preallocated batch_out_
                    std::array<uint8_t, 10> out_hdr{};
                    size_t hlen = serialize_frame_header(header.opcode, payload.size(), out_hdr.data(), true);
                    batch_out_.append(reinterpret_cast<const char*>(out_hdr.data()), hlen);
                    batch_out_.append(payload);
                } else {
                    // Dispatch to registered user callbacks
                    if (ws_.message_callback()) {
                        co_await ws_.message_callback()(ws_, Message(payload, header.opcode));
                    }
                    if (header.opcode == Opcode::Text && ws_.text_callback()) {
                        co_await ws_.text_callback()(ws_, payload);
                    } else if (header.opcode == Opcode::Binary && ws_.binary_callback()) {
                        co_await ws_.binary_callback()(ws_, std::span<const uint8_t>(payload_ptr, header.payload_len));
                    }
                }
            }
        }

        if (batch_out_.size() >= 65536) {
            (void)co_await transport_.write_raw(batch_out_);
            batch_out_.clear();
        }

        consumed += total_frame_len;
    }

    if (!batch_out_.empty()) {
        (void)co_await transport_.write_raw(batch_out_);
        batch_out_.clear();
    }

    // Manage remainder buffer
    if (stream_buf_.empty()) {
        if (consumed < data.size()) {
            stream_buf_.assign(data.substr(consumed));
        }
    } else {
        if (consumed >= stream_buf_.size()) {
            stream_buf_.clear();
        } else if (consumed > 0) {
            stream_buf_.erase(0, consumed);
        }
    }

    co_return true;
}

} // namespace aegon::http::websocket
