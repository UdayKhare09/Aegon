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
    glaze::glaze
    ${AEGON_DEFLATE_LIB}
    ${AEGON_NGHTTP2_LIB}
    ${AEGON_NGHTTP3_LIB}
    ${AEGON_PROTOBUF_LIB}
)

if(Protobuf_INCLUDE_DIRS)
    target_include_directories(aegon_http PUBLIC ${Protobuf_INCLUDE_DIRS})
endif()
if(WIN32)
    target_link_libraries(aegon_http PUBLIC ws2_32)
endif()

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
