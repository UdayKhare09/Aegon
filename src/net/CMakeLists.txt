# ==============================================================================
# Asynchronous Networking Engine (aegon_net)
# ==============================================================================

add_library(aegon_net
    SocketAddress.cpp
    SocketOptions.cpp
    tcp/TcpListener.cpp
    tcp/TcpConnector.cpp
    udp/UdpSocket.cpp
    tls/TlsContext.cpp
    tls/TlsStream.cpp
    quic/QuicConnection.cpp
    quic/QuicServer.cpp
)

target_link_libraries(aegon_net PUBLIC
    aegon_core
    ${AEGON_SSL_LIB}
    ${AEGON_CRYPTO_LIB}
    ${AEGON_NGTCP2_CRYPTO_LIB}
    ${AEGON_NGTCP2_LIB}
)

if(WIN32)
    target_link_libraries(aegon_net PUBLIC ws2_32)
endif()

target_precompile_headers(aegon_net PRIVATE
    <vector>
    <string>
    <string_view>
    <span>
    <memory>
    <optional>
    <chrono>
    <openssl/ssl.h>
    <openssl/crypto.h>
)
