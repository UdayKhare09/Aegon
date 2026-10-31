#pragma once

/**
 * @file Net.h
 * @brief Umbrella header for Aegon Net: High-performance asynchronous networking library.
 * Provides TCP, UDP, TLS 1.3/1.2, and QUIC primitives built on aegon_core.
 */

#include "net/SocketAddress.h"
#include "net/SocketOptions.h"
#include "net/tcp/TcpStream.h"
#include "net/tcp/TcpListener.h"
#include "net/tcp/TcpConnector.h"
#include "net/udp/UdpSocket.h"
#include "net/udp/UdpServer.h"
#include "net/udp/UdpClient.h"
#include "net/tls/TlsContext.h"
#include "net/tls/TlsStream.h"
#include "net/quic/QuicPacket.h"
#include "net/quic/QuicConnection.h"
#include "net/quic/QuicServer.h"

