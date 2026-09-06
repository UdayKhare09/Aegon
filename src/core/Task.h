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

    T& result() & {
        if (exception_) [[unlikely]] {
            std::rethrow_exception(exception_);
        }
        assert(value_.has_value());
        return *value_;
    }

    T&& result() && {
        if (exception_) [[unlikely]] {
            std::rethrow_exception(exception_);
        }
        assert(value_.has_value());
        return std::move(*value_);
    }
};

template <>
struct TaskPromise<void> final : TaskPromiseBase {
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}

    void result() {
        if (exception_) [[unlikely]] {
            std::rethrow_exception(exception_);
        }
    }
};

} // namespace detail

/**
 * @brief High-performance C++26 lazy coroutine task with symmetric transfer.
 *
 * Implements zero-cost coroutine chaining and symmetric transfer to prevent
 * stack-overflow during continuous network packet processing.
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

    auto operator co_await() const & = delete;

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

            decltype(auto) await_resume() {
                if (!coro_) [[unlikely]] {
                    throw std::runtime_error("co_awaiting an invalid Task");
                }
                return std::move(coro_.promise()).result();
            }
        };

        return Awaiter{coro_};
    }

    // Direct resume for entrypoint loops
    void resume() {
        if (coro_ && !coro_.done()) {
            coro_.resume();
        }
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
