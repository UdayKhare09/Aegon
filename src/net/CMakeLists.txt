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
    Aegon::Ext::OpenSSL_SSL
    Aegon::Ext::OpenSSL_Crypto
    Aegon::Ext::ngtcp2_crypto
    Aegon::Ext::ngtcp2
)

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
