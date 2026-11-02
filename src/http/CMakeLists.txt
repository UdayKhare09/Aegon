# ==============================================================================
# HTTP & WebSocket Server Engine (aegon_http)
# ==============================================================================

add_library(aegon_http
    RadixTree.cpp
    Router.cpp
    Server.cpp
    v1/Http1Connection.cpp
    v1/Http1Serializer.cpp
    v2/Http2Connection.cpp
    v3/Http3Connection.cpp
    v3/Http3Server.cpp
    client/TlsClientStream.cpp
    client/ConnectionPool.cpp
    client/HttpClient.cpp
    client/Http2ClientSession.cpp
    client/Http3ClientSession.cpp
    websocket/WebSocketSession.cpp
    websocket/WebSocketConnection.cpp
)

target_link_libraries(aegon_http PUBLIC
    aegon_net
    aegon_storage
    aegon_log
    Aegon::Ext::glaze
    Aegon::Ext::deflate
    Aegon::Ext::nghttp2
    Aegon::Ext::nghttp3
    Aegon::Ext::Protobuf
)

target_precompile_headers(aegon_http PRIVATE
    <vector>
    <string>
    <string_view>
    <span>
    <memory>
    <optional>
    <chrono>
    <array>
    <unordered_map>
    <functional>
    <glaze/glaze.hpp>
    <openssl/ssl.h>
    <openssl/crypto.h>
)
