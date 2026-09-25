#pragma once

#include <coroutine>
#include <exception>
#include <utility>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace aegon::core {

template <typename T = void>
class [[nodiscard]] Task;

namespace detail {

struct TaskPromiseBase {
    std::coroutine_handle<> continuation_{nullptr};
    std::exception_ptr exception_{nullptr};

    [[nodiscard]] auto initial_suspend() noexcept {
        return std::suspend_always{};
    }

    struct FinalAwaiter {
        [[nodiscard]] bool await_ready() noexcept { return false; }

        template <typename Promise>
        [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            // Symmetric transfer to the awaiting coroutine
            if (h.promise().continuation_) {
                return h.promise().continuation_;
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
