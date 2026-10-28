#pragma once

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/error.h>
#include <aegon/c/router.h>

#include <string>
#include <string_view>
#include <exception>
#include <new>
#include <system_error>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <memory>
#include "http/RouteGroup.h"

// Forward declarations for C++ engine types
namespace aegon::http {
class Context;
class Request;
class Response;
class Server;
class Router;
class RouteGroup;
namespace websocket {
class WebSocket;
}
} // namespace aegon::http

namespace aegon::c {
struct CAppData {
    void* ptr{nullptr};
};

struct CLocalsMap {
    std::unordered_map<std::string, void*> map;
};
} // namespace aegon::c

struct aegon_server {
    std::unique_ptr<aegon::http::Server> server;
    std::thread supervisor_thread;
    std::atomic<bool> async_running{false};
    void* app_data{nullptr};

    aegon_server();
    ~aegon_server();
};

struct aegon_group {
    aegon::http::RouteGroup grp;
    explicit aegon_group(aegon::http::RouteGroup g) : grp(std::move(g)) {}
};

namespace aegon::http {
using Next = std::function<core::Task<void>(Context&)>;
using MiddlewareFn = std::function<core::Task<void>(Context&, Next)>;
}

struct aegon_next {
    aegon::http::Next* next_fn{nullptr};
    bool called{false};
};

// Forward declarations for C opaque handles
typedef struct aegon_ctx aegon_ctx_t;
typedef struct aegon_req aegon_req_t;
typedef struct aegon_res aegon_res_t;
typedef struct aegon_server aegon_server_t;
typedef struct aegon_router aegon_router_t;
typedef struct aegon_group aegon_group_t;
typedef struct aegon_next aegon_next_t;
typedef struct aegon_async aegon_async_t;
typedef struct aegon_ws aegon_ws_t;
typedef struct aegon_sse aegon_sse_t;
typedef struct aegon_multipart aegon_multipart_t;

namespace aegon::c {

// Thread-local error reporting
void set_last_error(std::string_view msg) noexcept;
const char* get_last_error() noexcept;
void clear_last_error() noexcept;

inline aegon_err_t record_error(std::string_view msg, aegon_err_t err = AEGON_ERR_INTERNAL) noexcept {
    set_last_error(msg);
    return err;
}

inline aegon_err_t record_error(const std::exception& e, aegon_err_t fallback = AEGON_ERR_INTERNAL) noexcept {
    return record_error(e.what(), fallback);
}

// Zero-cost pointer casts (01-architecture.md §A3)
inline http::Context* to_cpp(aegon_ctx_t* c) noexcept {
    return reinterpret_cast<http::Context*>(c);
}
inline aegon_ctx_t* to_c(http::Context* c) noexcept {
    return reinterpret_cast<aegon_ctx_t*>(c);
}

inline http::Request* to_cpp(aegon_req_t* r) noexcept {
    return reinterpret_cast<http::Request*>(r);
}
inline aegon_req_t* to_c(http::Request* r) noexcept {
    return reinterpret_cast<aegon_req_t*>(r);
}

inline http::Response* to_cpp(aegon_res_t* r) noexcept {
    return reinterpret_cast<http::Response*>(r);
}
inline aegon_res_t* to_c(http::Response* r) noexcept {
    return reinterpret_cast<aegon_res_t*>(r);
}

inline aegon_server* to_server_handle(aegon_server_t* s) noexcept {
    return reinterpret_cast<aegon_server*>(s);
}
inline aegon_server_t* to_c(aegon_server* s) noexcept {
    return reinterpret_cast<aegon_server_t*>(s);
}
http::Server* to_cpp(aegon_server_t* s) noexcept;

inline http::Router* to_cpp(aegon_router_t* r) noexcept {
    return reinterpret_cast<http::Router*>(r);
}
inline aegon_router_t* to_c(http::Router* r) noexcept {
    return reinterpret_cast<aegon_router_t*>(r);
}

inline aegon_group* to_group_handle(aegon_group_t* g) noexcept {
    return reinterpret_cast<aegon_group*>(g);
}
inline aegon_group_t* to_c(aegon_group* g) noexcept {
    return reinterpret_cast<aegon_group_t*>(g);
}

inline aegon_next* to_next_handle(aegon_next_t* n) noexcept {
    return reinterpret_cast<aegon_next*>(n);
}
inline aegon_next_t* to_c(aegon_next* n) noexcept {
    return reinterpret_cast<aegon_next_t*>(n);
}

struct CleanupHolder {
    void* userdata{nullptr};
    aegon_cleanup_fn cleanup{nullptr};

    explicit CleanupHolder(void* ud, aegon_cleanup_fn fn) : userdata(ud), cleanup(fn) {}

    CleanupHolder(const CleanupHolder&) = delete;
    CleanupHolder& operator=(const CleanupHolder&) = delete;
    CleanupHolder(CleanupHolder&&) = delete;
    CleanupHolder& operator=(CleanupHolder&&) = delete;

    ~CleanupHolder() {
        if (cleanup) {
            cleanup(userdata);
            cleanup = nullptr;
        }
    }
};

inline std::shared_ptr<CleanupHolder> make_cleanup_holder(void* userdata, aegon_cleanup_fn cleanup) {
    if (!cleanup) return nullptr;
    return std::make_shared<CleanupHolder>(userdata, cleanup);
}

// String conversions
inline aegon_str_t to_c_str(std::string_view sv) noexcept {
    return aegon_str_t{ sv.data(), sv.size() };
}
inline std::string_view to_sv(aegon_str_t s) noexcept {
    return std::string_view{ s.data ? s.data : "", s.len };
}

} // namespace aegon::c

// Exception firewall macros (01-architecture.md §A4)
#define AEGON_C_TRY try

#define AEGON_C_CATCH                                                      \
    catch (const std::bad_alloc& e) {                                      \
        return ::aegon::c::record_error(e.what(), AEGON_ERR_NOMEM);       \
    } catch (const std::invalid_argument& e) {                             \
        return ::aegon::c::record_error(e.what(), AEGON_ERR_INVALID_ARG); \
    } catch (const std::system_error& e) {                                 \
        return ::aegon::c::record_error(e.what(), AEGON_ERR_IO);          \
    } catch (const std::exception& e) {                                    \
        return ::aegon::c::record_error(e.what(), AEGON_ERR_INTERNAL);    \
    } catch (...) {                                                        \
        return ::aegon::c::record_error("unknown C++ exception", AEGON_ERR_INTERNAL); \
    }

#define AEGON_C_CATCH_VAL(val)                                             \
    catch (const std::exception& e) {                                      \
        ::aegon::c::record_error(e.what());                               \
        return (val);                                                      \
    } catch (...) {                                                        \
        ::aegon::c::record_error("unknown C++ exception");                 \
        return (val);                                                      \
    }

#define AEGON_C_CATCH_VOID                                                 \
    catch (const std::exception& e) {                                      \
        ::aegon::c::record_error(e.what());                               \
        return;                                                            \
    } catch (...) {                                                        \
        ::aegon::c::record_error("unknown C++ exception");                 \
        return;                                                            \
    }
