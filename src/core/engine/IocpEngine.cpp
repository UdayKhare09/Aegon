#if defined(_WIN32)

#include "core/engine/IocpEngine.h"
#include "log/Logger.h"
#include <system_error>
#include <cstring>

namespace aegon::core {

namespace {

extern "C" ULONG NTAPI RtlNtStatusToDosError(LONG Status);

template <typename FnPtr>
FnPtr load_wsa_extension(SOCKET s, GUID guid) {
    FnPtr fn = nullptr;
    DWORD bytes = 0;
    int rc = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &guid, sizeof(guid),
                      &fn, sizeof(fn),
                      &bytes, nullptr, nullptr);
    if (rc != 0 || !fn) {
        log::error("[IocpEngine] Failed to load Winsock extension function: {}", WSAGetLastError());
        return nullptr;
    }
    return fn;
}

} // anonymous namespace

IocpEngine::IocpEngine(const EngineConfig& config, BufferPool* pool)
    : buffer_pool_(pool) {
    init_iocp(config.entries);
    log::debug("[IocpEngine] Initialized Windows IOCP backend");
}

IocpEngine::~IocpEngine() {
    if (iocp_ != INVALID_HANDLE_VALUE) {
        CloseHandle(iocp_);
        iocp_ = INVALID_HANDLE_VALUE;
    }
}

void IocpEngine::init_iocp(uint32_t /*entries*/) {
    init_sockets();

    // Create IOCP with concurrency hint 1 for thread-per-core event loop
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!iocp_) {
        DWORD err = GetLastError();
        log::error("[IocpEngine] CreateIoCompletionPort failed: {}", err);
        throw std::system_error(static_cast<int>(err), std::system_category(), "CreateIoCompletionPort failed");
    }

    load_extension_functions();
    initialized_ = true;
}

void IocpEngine::load_extension_functions() {
    SOCKET dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (dummy == INVALID_SOCKET) {
        dummy = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    }
    if (dummy == INVALID_SOCKET) {
        log::error("[IocpEngine] Failed to create probe socket for extensions: {}", WSAGetLastError());
        return;
    }

    GUID guid_accept_ex = WSAID_ACCEPTEX;
    GUID guid_get_accept_ex = WSAID_GETACCEPTEXSOCKADDRS;
    GUID guid_connect_ex = WSAID_CONNECTEX;
    GUID guid_transmit_file = WSAID_TRANSMITFILE;

    fn_accept_ex_ = load_wsa_extension<LPFN_ACCEPTEX>(dummy, guid_accept_ex);
    fn_get_accept_ex_sockaddrs_ = load_wsa_extension<LPFN_GETACCEPTEXSOCKADDRS>(dummy, guid_get_accept_ex);
    fn_connect_ex_ = load_wsa_extension<LPFN_CONNECTEX>(dummy, guid_connect_ex);
    fn_transmit_file_ = load_wsa_extension<LPFN_TRANSMITFILE>(dummy, guid_transmit_file);

    closesocket(dummy);
}

bool IocpEngine::associate_socket(os_socket_t sock) noexcept {
    if (sock == INVALID_OS_SOCKET || iocp_ == INVALID_HANDLE_VALUE) return false;
    HANDLE res = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), iocp_,
                                        static_cast<ULONG_PTR>(sock), 0);
    return res != nullptr;
}

void IocpEngine::wakeup() noexcept {
    if (iocp_ != INVALID_HANDLE_VALUE) {
        PostQueuedCompletionStatus(iocp_, 0, WAKEUP_KEY, nullptr);
    }
}

void IocpEngine::post_completion(IoAwaiter* aw, int res) noexcept {
    if (!aw || iocp_ == INVALID_HANDLE_VALUE) return;
    std::memset(&aw->overlapped, 0, sizeof(aw->overlapped));
    PostQueuedCompletionStatus(iocp_, static_cast<DWORD>(res), 0, &aw->overlapped);
}

int IocpEngine::submit_and_wait(uint32_t /*min_complete*/) {
    DWORD timeout_ms = INFINITE;

    if (!timers_.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (timers_.top().deadline <= now) {
            timeout_ms = 0;
        } else {
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                timers_.top().deadline - now
            ).count();
            timeout_ms = static_cast<DWORD>(std::max<int64_t>(0, diff));
        }
    }

    completed_count_ = 0;
    BOOL ok = GetQueuedCompletionStatusEx(
        iocp_,
        completion_entries_,
        static_cast<ULONG>(MAX_BATCH_COMPLETIONS),
        &completed_count_,
        timeout_ms,
        FALSE
    );

    if (!ok) {
        DWORD err = GetLastError();
        if (err != WAIT_TIMEOUT) {
            log::warn("[IocpEngine] GetQueuedCompletionStatusEx failed: {}", err);
        }
        completed_count_ = 0;
    }

    return static_cast<int>(completed_count_);
}

void IocpEngine::process_timers() noexcept {
    if (timers_.empty()) return;
    auto now = std::chrono::steady_clock::now();
    while (!timers_.empty() && timers_.top().deadline <= now) {
        auto entry = timers_.top();
        timers_.pop();
        if (entry.awaiter) {
            entry.awaiter->on_completion(0, 0);
        }
    }
}

size_t IocpEngine::process_completions() noexcept {
    process_timers();

    size_t processed = 0;
    for (ULONG i = 0; i < completed_count_; ++i) {
        const auto& entry = completion_entries_[i];

        // Wakeup packet has key WAKEUP_KEY and nullptr overlapped
        if (entry.lpCompletionKey == WAKEUP_KEY && entry.lpOverlapped == nullptr) {
            continue;
        }

        if (!entry.lpOverlapped) continue;

        auto* aw = CONTAINING_RECORD(entry.lpOverlapped, IoAwaiter, overlapped);
        if (!aw) continue;

        DWORD bytes = entry.dwNumberOfBytesTransferred;
        int status_code = static_cast<int>(bytes);
        uint32_t flags = 0;

        // Check NTSTATUS / Internal
        if (entry.Internal != 0) {
            DWORD win_err = RtlNtStatusToDosError(static_cast<LONG>(entry.Internal));
            status_code = -static_cast<int>(win_err);
        }

        aw->on_completion(status_code, flags);
        ++processed;
    }

    completed_count_ = 0;
    return processed;
}

// ---------------------------------------------------------------------------
// Awaiter Submissions
// ---------------------------------------------------------------------------

void IocpEngine::submit_accept(AcceptAwaiter& aw) noexcept {
    if (aw.listen_fd == INVALID_OS_SOCKET) {
        aw.result = -EINVAL;
        resume_continuation(aw);
        return;
    }

    associate_socket(aw.listen_fd);

    aw.client_sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (aw.client_sock == INVALID_OS_SOCKET) {
        aw.result = -static_cast<int>(WSAGetLastError());
        resume_continuation(aw);
        return;
    }

    associate_socket(aw.client_sock);

    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));
    DWORD bytes = 0;
    constexpr DWORD addr_size = sizeof(sockaddr_storage) + 16;

    if (!fn_accept_ex_) {
        aw.result = -ENOSYS;
        resume_continuation(aw);
        return;
    }

    BOOL ok = fn_accept_ex_(
        aw.listen_fd,
        aw.client_sock,
        aw.accept_buf,
        0,
        addr_size,
        addr_size,
        &bytes,
        &aw.overlapped
    );

    if (!ok) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            closesocket(aw.client_sock);
            aw.client_sock = INVALID_OS_SOCKET;
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_recv_provided(RecvProvidedAwaiter& aw) noexcept {
    if (!buffer_pool_) {
        aw.result = -ENOBUFS;
        resume_continuation(aw);
        return;
    }

    int bid = buffer_pool_->alloc_buffer();
    if (bid < 0) {
        aw.result = -ENOBUFS;
        resume_continuation(aw);
        return;
    }

    aw.allocated_bid = static_cast<uint16_t>(bid);
    aw.buffer_pool = buffer_pool_;
    aw.buffer_allocated = true;
    auto slice = buffer_pool_->get_buffer(aw.allocated_bid, static_cast<int>(buffer_pool_->buffer_size()));

    associate_socket(aw.socket_fd);
    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));

    aw.wsa_buf.buf = reinterpret_cast<CHAR*>(slice.data());
    aw.wsa_buf.len = static_cast<ULONG>(slice.size());

    DWORD flags = 0;
    int rc = WSARecv(aw.socket_fd, &aw.wsa_buf, 1, nullptr, &flags, &aw.overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            buffer_pool_->return_buffer(aw.allocated_bid);
            aw.buffer_allocated = false;
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_recv(RecvAwaiter& aw) noexcept {
    associate_socket(aw.socket_fd);
    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));

    aw.wsa_buf.buf = static_cast<CHAR*>(aw.buf);
    aw.wsa_buf.len = static_cast<ULONG>(aw.len);

    DWORD flags = static_cast<DWORD>(aw.flags_arg);
    int rc = WSARecv(aw.socket_fd, &aw.wsa_buf, 1, nullptr, &flags, &aw.overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_send(SendAwaiter& aw) noexcept {
    associate_socket(aw.socket_fd);
    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));

    aw.wsa_buf.buf = const_cast<CHAR*>(static_cast<const char*>(aw.buf));
    aw.wsa_buf.len = static_cast<ULONG>(aw.len);

    int rc = WSASend(aw.socket_fd, &aw.wsa_buf, 1, nullptr, 0, &aw.overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_send_zc(SendZcAwaiter& aw) noexcept {
    associate_socket(aw.socket_fd);
    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));

    aw.wsa_buf.buf = const_cast<CHAR*>(static_cast<const char*>(aw.buf));
    aw.wsa_buf.len = static_cast<ULONG>(aw.len);

    int rc = WSASend(aw.socket_fd, &aw.wsa_buf, 1, nullptr, 0, &aw.overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_splice(SpliceAwaiter& aw) noexcept {
    if (!fn_transmit_file_) {
        aw.result = -ENOSYS;
        resume_continuation(aw);
        return;
    }

    associate_socket(aw.fd_out);
    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));
    aw.overlapped.Offset = static_cast<DWORD>(aw.off_in & 0xFFFFFFFF);
    aw.overlapped.OffsetHigh = static_cast<DWORD>((aw.off_in >> 32) & 0xFFFFFFFF);

    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(aw.fd_in));
    BOOL ok = fn_transmit_file_(aw.fd_out, hFile, aw.nbytes, 0, &aw.overlapped, nullptr, 0);
    if (!ok) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_connect(ConnectAwaiter& aw) noexcept {
    if (!fn_connect_ex_) {
        aw.result = -ENOSYS;
        resume_continuation(aw);
        return;
    }

    associate_socket(aw.socket_fd);

    // ConnectEx requires the socket to be bound first
    sockaddr_storage any_addr{};
    int bind_len = sizeof(sockaddr_in);
    if (reinterpret_cast<const sockaddr*>(&aw.addr)->sa_family == AF_INET6) {
        auto* a6 = reinterpret_cast<sockaddr_in6*>(&any_addr);
        a6->sin6_family = AF_INET6;
        a6->sin6_addr = in6addr_any;
        a6->sin6_port = 0;
        bind_len = sizeof(sockaddr_in6);
    } else {
        auto* a4 = reinterpret_cast<sockaddr_in*>(&any_addr);
        a4->sin_family = AF_INET;
        a4->sin_addr.s_addr = INADDR_ANY;
        a4->sin_port = 0;
        bind_len = sizeof(sockaddr_in);
    }
    (void)::bind(aw.socket_fd, reinterpret_cast<const sockaddr*>(&any_addr), bind_len);

    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));
    DWORD bytes = 0;
    BOOL ok = fn_connect_ex_(
        aw.socket_fd,
        reinterpret_cast<const sockaddr*>(&aw.addr),
        static_cast<int>(aw.addr_len),
        nullptr,
        0,
        &bytes,
        &aw.overlapped
    );

    if (!ok) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_poll(PollAwaiter& aw) noexcept {
    // IOCP operates on completions, not readiness polling.
    // For general descriptor readiness on Windows, immediate resume.
    aw.result = 0;
    resume_continuation(aw);
}

void IocpEngine::submit_timeout(TimeoutAwaiter& aw) noexcept {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(aw.ns);
    timers_.push({deadline, &aw});
}

void IocpEngine::submit_close(CloseAwaiter& aw) noexcept {
    if (aw.fd != INVALID_OS_SOCKET) {
        closesocket(aw.fd);
    }
    aw.result = 0;
    resume_continuation(aw);
}

void IocpEngine::submit_shutdown(ShutdownAwaiter& aw) noexcept {
    if (aw.fd != INVALID_OS_SOCKET) {
        shutdown(aw.fd, aw.how);
    }
    aw.result = 0;
    resume_continuation(aw);
}

void IocpEngine::submit_cancel(CancelAwaiter& aw) noexcept {
    // In IOCP, cancellation of a handle is performed via CancelIoEx
    CancelIoEx(reinterpret_cast<HANDLE>(aw.user_data), nullptr);
    aw.result = 0;
    resume_continuation(aw);
}

void IocpEngine::submit_recvmsg(RecvmsgAwaiter& aw) noexcept {
    if (aw.socket_fd == INVALID_OS_SOCKET || !aw.msg || aw.msg->msg_iovlen == 0) {
        aw.result = -EINVAL;
        resume_continuation(aw);
        return;
    }

    associate_socket(aw.socket_fd);
    std::memset(&aw.overlapped, 0, sizeof(aw.overlapped));

    aw.wsa_buf.buf = reinterpret_cast<CHAR*>(aw.msg->msg_iov[0].iov_base);
    aw.wsa_buf.len = static_cast<ULONG>(aw.msg->msg_iov[0].iov_len);
    aw.from_len = static_cast<INT>(aw.msg->msg_namelen);

    DWORD flags = 0;
    int rc = WSARecvFrom(
        aw.socket_fd,
        &aw.wsa_buf,
        1,
        nullptr,
        &flags,
        reinterpret_cast<sockaddr*>(aw.msg->msg_name),
        &aw.from_len,
        &aw.overlapped,
        nullptr
    );

    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            aw.result = -err;
            resume_continuation(aw);
        }
    }
}

void IocpEngine::submit_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept {
    if (st.listen_fd == INVALID_OS_SOCKET || !fn_accept_ex_) {
        st.result = -EINVAL;
        resume_continuation(st);
        return;
    }

    associate_socket(st.listen_fd);

    st.client_sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (st.client_sock == INVALID_OS_SOCKET) {
        st.result = -static_cast<int>(WSAGetLastError());
        resume_continuation(st);
        return;
    }

    associate_socket(st.client_sock);

    std::memset(&st.overlapped, 0, sizeof(st.overlapped));
    DWORD bytes = 0;
    constexpr DWORD addr_size = sizeof(sockaddr_storage) + 16;

    st.armed = true;

    BOOL ok = fn_accept_ex_(
        st.listen_fd,
        st.client_sock,
        st.accept_buf,
        0,
        addr_size,
        addr_size,
        &bytes,
        &st.overlapped
    );

    if (!ok) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            closesocket(st.client_sock);
            st.client_sock = INVALID_OS_SOCKET;
            st.armed = false;
            st.result = -err;
            resume_continuation(st);
        }
    }
}

void IocpEngine::cancel_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept {
    if (st.armed) {
        if (st.client_sock != INVALID_OS_SOCKET) {
            CancelIoEx(reinterpret_cast<HANDLE>(st.listen_fd), &st.overlapped);
            closesocket(st.client_sock);
            st.client_sock = INVALID_OS_SOCKET;
        }
        st.armed = false;
        st.cancelled = true;
    }
}

void IocpEngine::submit_multishot_recv(MultishotRecvStream::StreamState& st) noexcept {
    if (!buffer_pool_) {
        st.result = -ENOBUFS;
        resume_continuation(st);
        return;
    }

    int bid = buffer_pool_->alloc_buffer();
    if (bid < 0) {
        st.result = -ENOBUFS;
        resume_continuation(st);
        return;
    }

    st.allocated_bid = static_cast<uint16_t>(bid);
    st.buffer_pool = buffer_pool_;
    st.buffer_allocated = true;
    auto slice = buffer_pool_->get_buffer(st.allocated_bid, static_cast<int>(buffer_pool_->buffer_size()));

    associate_socket(st.socket_fd);
    std::memset(&st.overlapped, 0, sizeof(st.overlapped));

    st.wsa_buf.buf = reinterpret_cast<CHAR*>(slice.data());
    st.wsa_buf.len = static_cast<ULONG>(slice.size());

    st.armed = true;

    DWORD flags = 0;
    int rc = WSARecv(st.socket_fd, &st.wsa_buf, 1, nullptr, &flags, &st.overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            buffer_pool_->return_buffer(st.allocated_bid);
            st.buffer_allocated = false;
            st.armed = false;
            st.result = -err;
            resume_continuation(st);
        }
    }
}

void IocpEngine::cancel_multishot_recv(MultishotRecvStream::StreamState& st) noexcept {
    if (st.armed) {
        CancelIoEx(reinterpret_cast<HANDLE>(st.socket_fd), &st.overlapped);
        st.armed = false;
        st.cancelled = true;
    }
}

} // namespace aegon::core

#endif // _WIN32
