#pragma once

#include <cstdio>
#include <cstdint>
#include <new>
#include <coroutine>
#include <exception>
#include <utility>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <cstdlib>

namespace aegon::core {

template <typename T = void>
class [[nodiscard]] Task;

namespace detail {

struct TaskPromiseBase;

// Intrusive list of an event loop's live root tasks (see EventLoop::spawn). Insert and
// unlink are O(1); the list is only walked when the loop is torn down.
struct RootTaskList {
    TaskPromiseBase* head{nullptr};
    size_t size{0};

    inline void link(TaskPromiseBase* p) noexcept;
    inline void unlink(TaskPromiseBase* p) noexcept;
    // Destroys every root still suspended (loop teardown).
    inline void destroy_all() noexcept;
};

inline void report_root_exception(const std::exception_ptr& e) noexcept {
    if (!e) return;
    try {
        std::rethrow_exception(e);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[EventLoop] Uncaught exception in root task: %s\n", ex.what());
    } catch (...) {
        std::fprintf(stderr, "[EventLoop] Uncaught unknown exception in root task\n");
    }
}

// Per-thread free lists of coroutine frames, by 64-byte size class up to 4 KiB. A server
// creates and destroys frames for every connection, request handler and async read, so
// recycling them keeps that off the general-purpose allocator. Frames are 64-byte aligned.
// A frame freed on another thread joins that thread's list, which is fine for a free list.
struct FramePool {
    static constexpr size_t kGranule = 64;
    static constexpr size_t kClasses = 64;            // up to 64 * 64 = 4096 bytes
    static constexpr uint32_t kMaxPerClass = 4096;    // beyond this, frames go back to the system

    struct Node { Node* next; };
    Node* heads[kClasses]{};
    uint32_t counts[kClasses]{};

    static size_t class_of(size_t size) noexcept { return (size + kGranule - 1) / kGranule - 1; }

    void* allocate(size_t size) {
        const size_t c = class_of(size);
        if (c < kClasses) {
            if (Node* n = heads[c]) {
                heads[c] = n->next;
                --counts[c];
                return n;
            }
            void* p = std::aligned_alloc(kGranule, (c + 1) * kGranule);
            if (!p) throw std::bad_alloc();
            return p;
        }
        void* p = nullptr;
        if (::posix_memalign(&p, kGranule, size) != 0) throw std::bad_alloc();
        return p;
    }

    void deallocate(void* p, size_t size) noexcept {
        const size_t c = class_of(size);
        if (c < kClasses && counts[c] < kMaxPerClass) {
            auto* n = static_cast<Node*>(p);
            n->next = heads[c];
            heads[c] = n;
            ++counts[c];
            return;
        }
        std::free(p);
    }

    ~FramePool() {
        for (size_t c = 0; c < kClasses; ++c) {
            while (Node* n = heads[c]) {
                heads[c] = n->next;
                std::free(n);
            }
        }
    }
};

inline FramePool& frame_pool() noexcept {
    thread_local FramePool pool;
    return pool;
}

struct TaskPromiseBase {
    static void* operator new(size_t size) {
        return frame_pool().allocate(size);
    }

    // Coroutine frames are freed with their size, which picks the size class.
    static void operator delete(void* ptr, size_t size) noexcept {
        frame_pool().deallocate(ptr, size);
    }

    std::coroutine_handle<> continuation_{nullptr};
    std::exception_ptr exception_{nullptr};
    // Set for a root task handed to EventLoop::spawn(): the frame destroys itself when it
    // finishes and unlinks itself from the loop's list of live roots, so the loop never
    // has to scan its tasks to find the finished ones.
    RootTaskList* root_list_{nullptr};
    TaskPromiseBase* root_prev_{nullptr};
    TaskPromiseBase* root_next_{nullptr};
    std::coroutine_handle<> root_handle_{nullptr};

    [[nodiscard]] auto initial_suspend() noexcept {
        return std::suspend_always{};
    }

    struct FinalAwaiter {
        [[nodiscard]] bool await_ready() noexcept { return false; }

        template <typename Promise>
        [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto& p = h.promise();
            // Symmetric transfer to the awaiting coroutine
            if (p.continuation_) {
                return p.continuation_;
            }
            if (p.root_list_) {
                p.root_list_->unlink(&p);
                report_root_exception(p.exception_);
                h.destroy();
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    [[nodiscard]] auto final_suspend() noexcept {
        return FinalAwaiter{};
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }
};

inline void RootTaskList::link(TaskPromiseBase* p) noexcept {
    p->root_list_ = this;
    p->root_prev_ = nullptr;
    p->root_next_ = head;
    if (head) head->root_prev_ = p;
    head = p;
    ++size;
}

inline void RootTaskList::unlink(TaskPromiseBase* p) noexcept {
    if (p->root_prev_) p->root_prev_->root_next_ = p->root_next_;
    else head = p->root_next_;
    if (p->root_next_) p->root_next_->root_prev_ = p->root_prev_;
    p->root_prev_ = p->root_next_ = nullptr;
    p->root_list_ = nullptr;
    --size;
}

inline void RootTaskList::destroy_all() noexcept {
    while (head) {
        TaskPromiseBase* p = head;
        auto h = p->root_handle_;
        unlink(p);
        h.destroy();
    }
}

template <typename T>
struct TaskPromise final : TaskPromiseBase {
    std::optional<T> value_{std::nullopt};

    Task<T> get_return_object() noexcept;

    template <typename U>
        requires std::convertible_to<U, T>
    void return_value(U&& val) noexcept(std::is_nothrow_constructible_v<T, U>) {
        value_.emplace(std::forward<U>(val));
    }

    // Non-destructive const& — safe to call multiple times after coroutine completes
    const T& result() const& {
        if (exception_) [[unlikely]] std::rethrow_exception(exception_);
        if (!value_.has_value()) [[unlikely]]
            throw std::runtime_error("Attempted to read result from uncompleted Task");
        return *value_;
    }

    // Non-destructive lvalue & — safe to call multiple times
    T& result() & {
        if (exception_) [[unlikely]] std::rethrow_exception(exception_);
        if (!value_.has_value()) [[unlikely]]
            throw std::runtime_error("Attempted to read result from uncompleted Task");
        return *value_;
    }

    // Destructive rvalue && — moves value out; used exclusively by co_await await_resume
    T&& result() && {
        if (exception_) [[unlikely]] std::rethrow_exception(exception_);
        if (!value_.has_value()) [[unlikely]]
            throw std::runtime_error("Attempted to read result from uncompleted Task");
        return std::move(*value_);
    }
};

template <>
struct TaskPromise<void> final : TaskPromiseBase {
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}

    // const overload: required so Task<void>::result() const& compiles
    void result() const {
        if (exception_) [[unlikely]] std::rethrow_exception(exception_);
    }

    void result() {
        if (exception_) [[unlikely]] std::rethrow_exception(exception_);
    }
};

} // namespace detail

/**
 * @brief High-performance C++ lazy coroutine task with symmetric transfer.
 *
 * Implements zero-cost coroutine chaining to prevent stack overflow during
 * continuous network packet processing.
 *
 * Result semantics:
 *  - result() &       : non-destructive reference; safe to call multiple times
 *  - result() const&  : same, on a const Task
 *  - take_result()    : moves the value out (single-use; explicit opt-in)
 *  - co_await         : internally uses move semantics (single-use by nature)
 */
template <typename T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept : coro_(nullptr) {}

    explicit Task(handle_type h) noexcept : coro_(h) {}

    Task(Task&& other) noexcept : coro_(std::exchange(other.coro_, nullptr)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, nullptr);
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (coro_) {
            coro_.destroy();
        }
    }

    [[nodiscard]] bool is_ready() const noexcept {
        return !coro_ || coro_.done();
    }

    // Disallow co_awaiting a named (lvalue) Task — prevents use-after-move bugs
    auto operator co_await() const& = delete;

    auto operator co_await() && noexcept {
        struct Awaiter {
            handle_type coro_;

            [[nodiscard]] bool await_ready() const noexcept {
                return !coro_ || coro_.done();
            }

            [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
                coro_.promise().continuation_ = awaiting;
                return coro_;
            }

            // co_await is single-use; move value for zero-copy forwarding
            decltype(auto) await_resume() {
                if (!coro_) [[unlikely]] {
                    throw std::runtime_error("co_awaiting an invalid Task");
                }
                return std::move(coro_.promise()).result();
            }
        };

        return Awaiter{coro_};
    }

    // Direct resume for entry-point loops (non-coroutine callers)
    void resume() {
        if (coro_ && !coro_.done()) {
            coro_.resume();
        }
    }

    /**
     * Non-destructive lvalue result — safe to call multiple times after is_ready().
     * Uses decltype(auto) so T=void degrades to void return without a specialization.
     */
    [[nodiscard]] decltype(auto) result() & {
        if (!coro_) [[unlikely]]
            throw std::runtime_error("Accessing result of an invalid Task");
        return coro_.promise().result();
    }

    /**
     * Non-destructive const result — safe on const Task.
     */
    [[nodiscard]] decltype(auto) result() const& {
        if (!coro_) [[unlikely]]
            throw std::runtime_error("Accessing result of an invalid Task");
        return coro_.promise().result();
    }

    /**
     * Destructive move — explicitly moves the stored value out of the task.
     * Calling result() after take_result() is undefined behavior.
     * Not available for Task<void>.
     */
    template <typename U = T>
        requires (!std::is_void_v<U>)
    [[nodiscard]] U take_result() {
        if (!coro_) [[unlikely]]
            throw std::runtime_error("Accessing result of an invalid Task");
        return std::move(coro_.promise()).result();
    }

    /**
     * Gives up ownership of the coroutine frame and returns its handle (nullptr if empty).
     * The caller becomes responsible for destroying it.
     */
    [[nodiscard]] handle_type release() noexcept {
        return std::exchange(coro_, nullptr);
    }

private:
    handle_type coro_;
};

namespace detail {

template <typename T>
inline Task<T> TaskPromise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
}

} // namespace detail

} // namespace aegon::core
