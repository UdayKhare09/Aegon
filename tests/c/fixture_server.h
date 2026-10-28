#ifndef AEGON_C_FIXTURE_SERVER_H
#define AEGON_C_FIXTURE_SERVER_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <aegon/c/aegon.h>
#include "check.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCK(s) closesocket(s)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
typedef int socket_t;
#define CLOSE_SOCK(s) close(s)
#define INVALID_SOCKET (-1)
#endif

static inline void fixture_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static inline uint16_t get_ephemeral_port(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd != INVALID_SOCKET);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0;

    int rc = bind(fd, (struct sockaddr*)&sin, sizeof(sin));
    CHECK(rc == 0);

    socklen_t len = sizeof(sin);
    rc = getsockname(fd, (struct sockaddr*)&sin, &len);
    CHECK(rc == 0);

    uint16_t port = ntohs(sin.sin_port);
    CLOSE_SOCK(fd);
    return port;
}

static inline int http_exchange(uint16_t port, const char* req, char* resp, size_t max_resp) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons(port);

    /* Retry connection briefly in case server is just binding */
    int connected = 0;
    for (int retry = 0; retry < 50; ++retry) {
        if (connect(fd, (struct sockaddr*)&sin, sizeof(sin)) == 0) {
            connected = 1;
            break;
        }
        fixture_sleep_ms(10);
    }

    if (!connected) {
        CLOSE_SOCK(fd);
        return -1;
    }

    size_t req_len = strlen(req);
    size_t total_sent = 0;
    while (total_sent < req_len) {
        int sent = send(fd, req + total_sent, (int)(req_len - total_sent), 0);
        if (sent <= 0) {
            CLOSE_SOCK(fd);
            return -1;
        }
        total_sent += (size_t)sent;
    }

    /* Set 2 second receive timeout */
#ifdef _WIN32
    DWORD tv = 2000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    size_t total_read = 0;
    while (total_read + 1 < max_resp) {
        int n = recv(fd, resp + total_read, (int)(max_resp - 1 - total_read), 0);
        if (n <= 0) break;
        total_read += (size_t)n;
        resp[total_read] = '\0';

        /* Fast exit if headers and Content-Length body have completely arrived */
        char* hdr_end = strstr(resp, "\r\n\r\n");
        if (hdr_end) {
            /* 204 No Content and 304 Not Modified have no message body per RFC 9110 */
            if (strstr(resp, "204") != NULL || strstr(resp, "304") != NULL) {
                break;
            }
            char* cl = strstr(resp, "Content-Length: ");
            if (!cl) cl = strstr(resp, "content-length: ");
            if (cl && cl < hdr_end) {
                long cl_val = strtol(cl + 16, NULL, 10);
                size_t body_received = total_read - (size_t)(hdr_end + 4 - resp);
                if (body_received >= (size_t)cl_val) {
                    break;
                }
            }
        }
    }
    resp[total_read] = '\0';
    CLOSE_SOCK(fd);
    return (int)total_read;
}

#endif /* AEGON_C_FIXTURE_SERVER_H */
