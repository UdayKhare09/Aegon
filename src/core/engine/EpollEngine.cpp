#include "core/engine/EpollEngine.h"
#include "core/BufferPool.h"
#include "core/ThreadPool.h"
#include "log/Logger.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <system_error>

namespace aegon::core {

EpollEngine::EpollEngine(const EngineConfig& config, BufferPool* pool) {
    init(config);
    if (pool) {
        buffer_pool_ = pool;
    } else {
        owned_buffer_pool_ = std::make_unique<BufferPool>(1, 2048, 4096);
        buffer_pool_ = owned_buffer_pool_.get();
    }
}

EpollEngine::~EpollEngine() {
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
    if (epfd_ >= 0) {
        ::close(epfd_);
        epfd_ = -1;
    }
}

EpollEngine::EpollEngine(EpollEngine&& other) noexcept
    : epfd_(other.epfd_),
      wakeup_fd_(other.wakeup_fd_),
      buffer_pool_(other.buffer_pool_),
      owned_buffer_pool_(std::move(other.owned_buffer_pool_)),
      slots_(std::move(other.slots_)),
      ep_events_(std::move(other.ep_events_)),
      ready_completions_(std::move(other.ready_completions_)),
      timers_(std::move(other.timers_)),
      next_timer_id_(other.next_timer_id_) {
    other.epfd_ = -1;
    other.wakeup_fd_ = -1;
    other.buffer_pool_ = nullptr;
}

EpollEngine& EpollEngine::operator=(EpollEngine&& other) noexcept {
    if (this != &other) {
        if (wakeup_fd_ >= 0) ::close(wakeup_fd_);
        if (epfd_ >= 0) ::close(epfd_);

        epfd_ = other.epfd_;
        wakeup_fd_ = other.wakeup_fd_;
        buffer_pool_ = other.buffer_pool_;
        owned_buffer_pool_ = std::move(other.owned_buffer_pool_);
        slots_ = std::move(other.slots_);
        ep_events_ = std::move(other.ep_events_);
        ready_completions_ = std::move(other.ready_completions_);
        timers_ = std::move(other.timers_);
        next_timer_id_ = other.next_timer_id_;

        other.epfd_ = -1;
        other.wakeup_fd_ = -1;
        other.buffer_pool_ = nullptr;
    }
    return *this;
}

void EpollEngine::init(const EngineConfig& config) {
    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) {
        int err = errno;
        log::error("[EpollEngine] epoll_create1 failed: {}", std::strerror(err));
        throw std::system_error(err, std::generic_category(), "epoll_create1 failed");
    }

    uint32_t entries = config.entries > 0 ? config.entries : 4096;
    ep_events_.resize(std::min<size_t>(entries, 2048));
    slots_.resize(1024);

    wakeup_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeup_fd_ >= 0) {
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = wakeup_fd_;
        if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeup_fd_, &ev) != 0) {
            log::warn("[EpollEngine] Failed to add wakeup_fd to epoll: {}", std::strerror(errno));
        }
    }

    log::debug("[EpollEngine] Initialized epoll fd={}, event buffer={}", epfd_, ep_events_.size());
}

void EpollEngine::register_buffer_pool(BufferPool& pool, bool /*register_buffers*/) {
    buffer_pool_ = &pool;
}

void EpollEngine::wakeup() noexcept {
    if (wakeup_fd_ >= 0) {
        uint64_t one = 1;
        (void)::write(wakeup_fd_, &one, sizeof(one));
    }
}

void EpollEngine::add_interest(int fd, uint32_t event_flag, IoAwaiter* aw, InterestType type) noexcept {
    if (fd < 0 || epfd_ < 0) return;

    if (static_cast<size_t>(fd) >= slots_.size()) {
        slots_.resize(static_cast<size_t>(fd) + 256);
    }

    FdSlot& slot = slots_[fd];
    uint32_t old_mask = slot.epoll_mask;

    if (event_flag & EPOLLIN) {
        slot.in_awaiter = aw;
        slot.in_type = type;
        slot.epoll_mask |= EPOLLIN;
    }
    if (event_flag & EPOLLOUT) {
        slot.out_awaiter = aw;
        slot.out_type = type;
        slot.epoll_mask |= EPOLLOUT;
    }

    struct epoll_event ev{};
    ev.events = slot.epoll_mask | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;

    int op = (old_mask == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    if (::epoll_ctl(epfd_, op, fd, &ev) != 0) {
        if (op == EPOLL_CTL_ADD && errno == EEXIST) {
            (void)::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
        } else {
            log::warn("[EpollEngine] epoll_ctl op={} fd={} failed: {}", op, fd, std::strerror(errno));
        }
    }
}

void EpollEngine::remove_interest(int fd, uint32_t event_flag) noexcept {
    if (fd < 0 || static_cast<size_t>(fd) >= slots_.size() || epfd_ < 0) return;

    FdSlot& slot = slots_[fd];
    if (event_flag & EPOLLIN) {
        slot.in_awaiter = nullptr;
        slot.in_type = InterestType::None;
        slot.epoll_mask &= ~EPOLLIN;
    }
    if (event_flag & EPOLLOUT) {
        slot.out_awaiter = nullptr;
        slot.out_type = InterestType::None;
        slot.epoll_mask &= ~EPOLLOUT;
    }

    if (slot.epoll_mask == 0) {
        (void)::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    } else {
        struct epoll_event ev{};
        ev.events = slot.epoll_mask | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        ev.data.fd = fd;
        (void)::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void EpollEngine::remove_all_interests(int fd) noexcept {
    if (fd < 0 || static_cast<size_t>(fd) >= slots_.size() || epfd_ < 0) return;

    FdSlot& slot = slots_[fd];
    slot.in_awaiter = nullptr;
    slot.out_awaiter = nullptr;
    slot.in_type = InterestType::None;
    slot.out_type = InterestType::None;
    slot.epoll_mask = 0;

    (void)::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
}

void EpollEngine::check_timers() noexcept {
    if (timers_.empty()) return;

    auto now = std::chrono::steady_clock::now();
    while (!timers_.empty() && timers_.top().deadline <= now) {
        TimerEntry entry = timers_.top();
        timers_.pop();
        if (entry.awaiter) {
            ready_completions_.push_back({entry.awaiter, 0, 0});
        }
    }
}

void EpollEngine::process_epoll_event(int fd, uint32_t events) noexcept {
    if (fd == wakeup_fd_) {
        uint64_t val = 0;
        (void)::read(wakeup_fd_, &val, sizeof(val));
        return;
    }

    if (fd < 0 || static_cast<size_t>(fd) >= slots_.size()) return;
    FdSlot& slot = slots_[fd];

    bool has_err = (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;

    // Handle IN events
    if ((events & EPOLLIN) || (has_err && slot.in_awaiter)) {
        IoAwaiter* aw = slot.in_awaiter;
        InterestType type = slot.in_type;

        switch (type) {
            case InterestType::Accept: {
                auto* a = static_cast<AcceptAwaiter*>(aw);
                int client_fd = ::accept4(a->listen_fd, reinterpret_cast<sockaddr*>(&a->addr), &a->addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client_fd >= 0) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, client_fd, 0});
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, -errno, 0});
                }
                break;
            }
            case InterestType::MultishotAccept: {
                auto* st = static_cast<MultishotAcceptStream::StreamState*>(aw);
                while (slot.in_awaiter == st && st->armed && !st->cancelled && !st->finished) {
                    sockaddr_storage addr{};
                    socklen_t addrlen = sizeof(addr);
                    int client_fd = ::accept4(st->listen_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (client_fd >= 0) {
                        st->on_completion(client_fd, 0x02 /* IORING_CQE_F_MORE */);
                        if (slot.in_awaiter != st) {
                            break;
                        }
                    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    } else {
                        remove_interest(fd, EPOLLIN);
                        st->on_completion(-errno, 0);
                        break;
                    }
                }
                break;
            }
            case InterestType::RecvProvided: {
                auto* a = static_cast<RecvProvidedAwaiter*>(aw);
                if (!buffer_pool_) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, -ENOBUFS, 0});
                    break;
                }
                int bid = buffer_pool_->alloc_buffer();
                if (bid < 0) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, -ENOBUFS, 0});
                    break;
                }
                auto span = buffer_pool_->get_buffer(static_cast<uint16_t>(bid), buffer_pool_->buffer_size());
                ssize_t n = ::recv(a->socket_fd, span.data(), span.size(), MSG_DONTWAIT);
                if (n > 0) {
                    remove_interest(fd, EPOLLIN);
                    uint32_t flags = (static_cast<uint32_t>(bid) << 16) | 0x01;
                    ready_completions_.push_back({a, static_cast<int>(n), flags});
                } else if (n == 0) {
                    buffer_pool_->return_buffer(static_cast<uint16_t>(bid));
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, 0, 0});
                } else {
                    buffer_pool_->return_buffer(static_cast<uint16_t>(bid));
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        remove_interest(fd, EPOLLIN);
                        ready_completions_.push_back({a, -errno, 0});
                    }
                }
                break;
            }
            case InterestType::MultishotRecv: {
                auto* st = static_cast<MultishotRecvStream::StreamState*>(aw);
                while (slot.in_awaiter == st && st->armed && !st->cancelled && !st->finished) {
                    if (!buffer_pool_) {
                        remove_interest(fd, EPOLLIN);
                        st->on_completion(-ENOBUFS, 0);
                        break;
                    }
                    int bid = buffer_pool_->alloc_buffer();
                    if (bid < 0) {
                        remove_interest(fd, EPOLLIN);
                        st->on_completion(-ENOBUFS, 0);
                        break;
                    }
                    auto span = buffer_pool_->get_buffer(static_cast<uint16_t>(bid), buffer_pool_->buffer_size());
                    ssize_t n = ::recv(st->socket_fd, span.data(), span.size(), MSG_DONTWAIT);
                    if (n > 0) {
                        uint32_t flags = (static_cast<uint32_t>(bid) << 16) | 0x01 | 0x02;
                        st->on_completion(static_cast<int>(n), flags);
                        if (slot.in_awaiter != st) {
                            break;
                        }
                    } else if (n == 0) {
                        buffer_pool_->return_buffer(static_cast<uint16_t>(bid));
                        remove_interest(fd, EPOLLIN);
                        st->on_completion(0, 0);
                        break;
                    } else {
                        buffer_pool_->return_buffer(static_cast<uint16_t>(bid));
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else {
                            remove_interest(fd, EPOLLIN);
                            st->on_completion(-errno, 0);
                            break;
                        }
                    }
                }
                break;
            }
            case InterestType::Recv: {
                auto* a = static_cast<RecvAwaiter*>(aw);
                ssize_t n = ::recv(a->socket_fd, a->buf, a->len, a->flags_arg | MSG_DONTWAIT);
                if (n >= 0) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, static_cast<int>(n), 0});
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, -errno, 0});
                }
                break;
            }
            case InterestType::Recvmsg: {
                auto* a = static_cast<RecvmsgAwaiter*>(aw);
                ssize_t n = ::recvmsg(a->socket_fd, a->msg, MSG_DONTWAIT);
                if (n >= 0) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, static_cast<int>(n), 0});
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove_interest(fd, EPOLLIN);
                    ready_completions_.push_back({a, -errno, 0});
                }
                break;
            }
            case InterestType::Poll: {
                auto* a = static_cast<PollAwaiter*>(aw);
                int revents = 0;
                if (events & EPOLLIN)  revents |= POLLIN;
                if (events & EPOLLERR) revents |= POLLERR;
                if (events & EPOLLHUP) revents |= POLLHUP;
                if (events & EPOLLRDHUP) revents |= POLLRDHUP;
                remove_interest(fd, EPOLLIN);
                ready_completions_.push_back({a, revents, 0});
                break;
            }
            default:
                break;
        }
    }

    // Handle OUT events
    if ((events & EPOLLOUT) || (has_err && slot.out_awaiter)) {
        IoAwaiter* aw = slot.out_awaiter;
        InterestType type = slot.out_type;

        switch (type) {
            case InterestType::Send: {
                auto* a = static_cast<SendAwaiter*>(aw);
                ssize_t n = ::send(a->socket_fd, a->buf, a->len, MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n >= 0) {
                    remove_interest(fd, EPOLLOUT);
                    ready_completions_.push_back({a, static_cast<int>(n), 0});
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove_interest(fd, EPOLLOUT);
                    ready_completions_.push_back({a, -errno, 0});
                }
                break;
            }
            case InterestType::SendZc: {
                auto* a = static_cast<SendZcAwaiter*>(aw);
                ssize_t n = ::send(a->socket_fd, a->buf, a->len, MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n >= 0) {
                    remove_interest(fd, EPOLLOUT);
                    ready_completions_.push_back({a, static_cast<int>(n), 0});
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove_interest(fd, EPOLLOUT);
                    ready_completions_.push_back({a, -errno, 0});
                }
                break;
            }
            case InterestType::Connect: {
                auto* a = static_cast<ConnectAwaiter*>(aw);
                int err = 0;
                socklen_t len = sizeof(err);
                int rc = ::getsockopt(a->socket_fd, SOL_SOCKET, SO_ERROR, &err, &len);
                remove_interest(fd, EPOLLOUT);
                if (rc == 0 && err == 0) {
                    ready_completions_.push_back({a, 0, 0});
                } else {
                    ready_completions_.push_back({a, err ? -err : -errno, 0});
                }
                break;
            }
            case InterestType::Splice: {
                auto* a = static_cast<SpliceAwaiter*>(aw);
                ssize_t n = ::splice(a->fd_in, a->off_in >= 0 ? &a->off_in : nullptr,
                                     a->fd_out, a->off_out >= 0 ? &a->off_out : nullptr,
                                     a->nbytes, a->splice_flags | SPLICE_F_NONBLOCK);
                if (n >= 0) {
                    remove_interest(fd, EPOLLOUT);
                    ready_completions_.push_back({a, static_cast<int>(n), 0});
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove_interest(fd, EPOLLOUT);
                    ready_completions_.push_back({a, -errno, 0});
                }
                break;
            }
            case InterestType::Poll: {
                auto* a = static_cast<PollAwaiter*>(aw);
                int revents = 0;
                if (events & EPOLLOUT) revents |= POLLOUT;
                if (events & EPOLLERR) revents |= POLLERR;
                if (events & EPOLLHUP) revents |= POLLHUP;
                remove_interest(fd, EPOLLOUT);
                ready_completions_.push_back({a, revents, 0});
                break;
            }
            default:
                break;
        }
    }
}

int EpollEngine::submit_and_wait(uint32_t min_complete) {
    if (epfd_ < 0) return -1;

    int timeout_ms = -1;
    if (!ready_completions_.empty()) {
        timeout_ms = 0;
    } else if (!timers_.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (timers_.top().deadline <= now) {
            timeout_ms = 0;
        } else {
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(timers_.top().deadline - now).count();
            timeout_ms = static_cast<int>(std::max<int64_t>(1, diff));
        }
    }

    if (min_complete == 0 && timeout_ms < 0) {
        timeout_ms = 0;
    }

    int nfds = ::epoll_wait(epfd_, ep_events_.data(), static_cast<int>(ep_events_.size()), timeout_ms);
    if (nfds < 0 && errno != EINTR) {
        log::error("[EpollEngine] epoll_wait failed: {}", std::strerror(errno));
        return -errno;
    }

    for (int i = 0; i < nfds; ++i) {
        process_epoll_event(ep_events_[i].data.fd, ep_events_[i].events);
    }

    check_timers();

    return static_cast<int>(ready_completions_.size());
}

size_t EpollEngine::process_completions() noexcept {
    {
        std::lock_guard<std::mutex> lock(posted_mutex_);
        while (!posted_completions_.empty()) {
            ready_completions_.push_back(posted_completions_.front());
            posted_completions_.pop_front();
        }
    }

    if (ready_completions_.empty()) return 0;

    auto ready = std::move(ready_completions_);
    ready_completions_.clear();

    size_t count = ready.size();
    for (auto& item : ready) {
        if (item.awaiter) {
            item.awaiter->on_completion(item.res, item.flags);
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Awaiter Submissions
// ---------------------------------------------------------------------------

void EpollEngine::submit_accept(AcceptAwaiter& aw) noexcept {
    int flags = ::fcntl(aw.listen_fd, F_GETFL, 0);
    if (flags >= 0 && !(flags & O_NONBLOCK)) {
        ::fcntl(aw.listen_fd, F_SETFL, flags | O_NONBLOCK);
    }
    int client_fd = ::accept4(aw.listen_fd, reinterpret_cast<sockaddr*>(&aw.addr), &aw.addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd >= 0) {
        ready_completions_.push_back({&aw, client_fd, 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.listen_fd, EPOLLIN, &aw, InterestType::Accept);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept {
    st.armed = true;
    int flags = ::fcntl(st.listen_fd, F_GETFL, 0);
    if (flags >= 0 && !(flags & O_NONBLOCK)) {
        ::fcntl(st.listen_fd, F_SETFL, flags | O_NONBLOCK);
    }
    add_interest(st.listen_fd, EPOLLIN, &st, InterestType::MultishotAccept);
}

void EpollEngine::cancel_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept {
    if (st.armed) {
        remove_interest(st.listen_fd, EPOLLIN);
        st.armed = false;
        st.cancelled = true;
        st.finished = true;
    }
}

void EpollEngine::submit_recv_provided(RecvProvidedAwaiter& aw) noexcept {
    if (!buffer_pool_) {
        ready_completions_.push_back({&aw, -ENOBUFS, 0});
        return;
    }
    int bid = buffer_pool_->alloc_buffer();
    if (bid < 0) {
        ready_completions_.push_back({&aw, -ENOBUFS, 0});
        return;
    }
    auto span = buffer_pool_->get_buffer(static_cast<uint16_t>(bid), buffer_pool_->buffer_size());
    ssize_t n = ::recv(aw.socket_fd, span.data(), span.size(), MSG_DONTWAIT);
    if (n > 0) {
        uint32_t flags = (static_cast<uint32_t>(bid) << 16) | 0x01;
        ready_completions_.push_back({&aw, static_cast<int>(n), flags});
        return;
    }
    buffer_pool_->return_buffer(static_cast<uint16_t>(bid));
    if (n == 0) {
        ready_completions_.push_back({&aw, 0, 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.socket_fd, EPOLLIN, &aw, InterestType::RecvProvided);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_multishot_recv(MultishotRecvStream::StreamState& st) noexcept {
    st.armed = true;
    int flags = ::fcntl(st.socket_fd, F_GETFL, 0);
    if (flags >= 0 && !(flags & O_NONBLOCK)) {
        ::fcntl(st.socket_fd, F_SETFL, flags | O_NONBLOCK);
    }
    add_interest(st.socket_fd, EPOLLIN, &st, InterestType::MultishotRecv);
}

void EpollEngine::cancel_multishot_recv(MultishotRecvStream::StreamState& st) noexcept {
    if (st.armed) {
        remove_interest(st.socket_fd, EPOLLIN);
        st.armed = false;
        st.cancelled = true;
        st.finished = true;
    }
}

void EpollEngine::submit_recv(RecvAwaiter& aw) noexcept {
    ssize_t n = ::recv(aw.socket_fd, aw.buf, aw.len, aw.flags_arg | MSG_DONTWAIT);
    if (n >= 0) {
        ready_completions_.push_back({&aw, static_cast<int>(n), 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.socket_fd, EPOLLIN, &aw, InterestType::Recv);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_send(SendAwaiter& aw) noexcept {
    ssize_t n = ::send(aw.socket_fd, aw.buf, aw.len, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n >= 0) {
        ready_completions_.push_back({&aw, static_cast<int>(n), 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.socket_fd, EPOLLOUT, &aw, InterestType::Send);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_send_zc(SendZcAwaiter& aw) noexcept {
    ssize_t n = ::send(aw.socket_fd, aw.buf, aw.len, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n >= 0) {
        ready_completions_.push_back({&aw, static_cast<int>(n), 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.socket_fd, EPOLLOUT, &aw, InterestType::SendZc);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_splice(SpliceAwaiter& aw) noexcept {
    ssize_t n = ::splice(aw.fd_in, aw.off_in >= 0 ? &aw.off_in : nullptr,
                         aw.fd_out, aw.off_out >= 0 ? &aw.off_out : nullptr,
                         aw.nbytes, aw.splice_flags | SPLICE_F_NONBLOCK);
    if (n >= 0) {
        ready_completions_.push_back({&aw, static_cast<int>(n), 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.fd_out, EPOLLOUT, &aw, InterestType::Splice);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_connect(ConnectAwaiter& aw) noexcept {
    int rc = ::connect(aw.socket_fd, reinterpret_cast<const sockaddr*>(&aw.addr), aw.addr_len);
    if (rc == 0) {
        ready_completions_.push_back({&aw, 0, 0});
        return;
    }
    if (errno == EINPROGRESS) {
        add_interest(aw.socket_fd, EPOLLOUT, &aw, InterestType::Connect);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::submit_poll(PollAwaiter& aw) noexcept {
    uint32_t mask = 0;
    if (aw.poll_mask & POLLIN)  mask |= EPOLLIN;
    if (aw.poll_mask & POLLOUT) mask |= EPOLLOUT;
    if (aw.poll_mask & POLLERR) mask |= EPOLLERR;
    if (aw.poll_mask & POLLHUP) mask |= EPOLLHUP;
    if (aw.poll_mask & POLLPRI) mask |= EPOLLPRI;

    if (mask & EPOLLIN) {
        add_interest(aw.fd, EPOLLIN, &aw, InterestType::Poll);
    } else if (mask & EPOLLOUT) {
        add_interest(aw.fd, EPOLLOUT, &aw, InterestType::Poll);
    } else {
        ready_completions_.push_back({&aw, 0, 0});
    }
}

void EpollEngine::submit_timeout(TimeoutAwaiter& aw) noexcept {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(aw.ns);
    timers_.push(TimerEntry{
        .deadline = deadline,
        .awaiter = &aw,
        .id = ++next_timer_id_
    });
}

void EpollEngine::submit_close(CloseAwaiter& aw) noexcept {
    remove_all_interests(aw.fd);
    int rc = ::close(aw.fd);
    ready_completions_.push_back({&aw, rc == 0 ? 0 : -errno, 0});
}

void EpollEngine::submit_shutdown(ShutdownAwaiter& aw) noexcept {
    int rc = ::shutdown(aw.fd, aw.how);
    ready_completions_.push_back({&aw, rc == 0 ? 0 : -errno, 0});
}

void EpollEngine::submit_cancel(CancelAwaiter& aw) noexcept {
    // Search ready completions
    bool found = false;
    for (auto it = ready_completions_.begin(); it != ready_completions_.end(); ++it) {
        if (reinterpret_cast<uint64_t>(it->awaiter) == aw.user_data) {
            it->res = -ECANCELED;
            found = true;
            break;
        }
    }
    ready_completions_.push_back({&aw, found ? 0 : -ENOENT, 0});
}

void EpollEngine::submit_recvmsg(RecvmsgAwaiter& aw) noexcept {
    ssize_t n = ::recvmsg(aw.socket_fd, aw.msg, MSG_DONTWAIT);
    if (n >= 0) {
        ready_completions_.push_back({&aw, static_cast<int>(n), 0});
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        add_interest(aw.socket_fd, EPOLLIN, &aw, InterestType::Recvmsg);
    } else {
        ready_completions_.push_back({&aw, -errno, 0});
    }
}

void EpollEngine::post_completion(IoAwaiter* aw, int res) noexcept {
    {
        std::lock_guard<std::mutex> lock(posted_mutex_);
        posted_completions_.push_back({aw, res, 0});
    }
    wakeup();
}

void EpollEngine::submit_read(ReadAwaiter& aw) noexcept {
#if defined(RWF_NOWAIT)
    struct iovec iov{aw.buf, aw.len};
    ssize_t n = ::preadv2(aw.fd, &iov, 1, aw.offset, RWF_NOWAIT);
    if (n >= 0) {
        ready_completions_.push_back({&aw, static_cast<int>(n), 0});
        return;
    }
#endif

    ThreadPool::default_pool().post([this, &aw]() {
        ssize_t ret;
        if (aw.offset >= 0) {
            ret = ::pread(aw.fd, aw.buf, aw.len, aw.offset);
        } else {
            ret = ::read(aw.fd, aw.buf, aw.len);
        }
        int res = (ret >= 0) ? static_cast<int>(ret) : -errno;
        this->post_completion(&aw, res);
    });
}

void EpollEngine::submit_write(WriteAwaiter& aw) noexcept {
    ThreadPool::default_pool().post([this, &aw]() {
        ssize_t ret;
        if (aw.offset >= 0) {
            ret = ::pwrite(aw.fd, aw.buf, aw.len, aw.offset);
        } else {
            ret = ::write(aw.fd, aw.buf, aw.len);
        }
        int res = (ret >= 0) ? static_cast<int>(ret) : -errno;
        this->post_completion(&aw, res);
    });
}

void EpollEngine::submit_fsync(FsyncAwaiter& aw) noexcept {
    ThreadPool::default_pool().post([this, &aw]() {
        int ret;
        if (aw.fsync_flags & 1 /* IORING_FSYNC_DATASYNC */) {
            ret = ::fdatasync(aw.fd);
        } else {
            ret = ::fsync(aw.fd);
        }
        int res = (ret == 0) ? 0 : -errno;
        this->post_completion(&aw, res);
    });
}

void EpollEngine::submit_fallocate(FallocateAwaiter& aw) noexcept {
    ThreadPool::default_pool().post([this, &aw]() {
        int ret = ::fallocate(aw.fd, aw.mode, aw.offset, aw.len);
        int res = (ret == 0) ? 0 : -errno;
        this->post_completion(&aw, res);
    });
}

} // namespace aegon::core
