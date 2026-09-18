# Error Handling & Problem Details

Aegon treats error handling as a first-class citizen. Every incoming HTTP request is executed within an asynchronous **global exception boundary** in `Router::dispatch()`.

Unhandled exceptions never crash the server or hang open connections. Instead, they are caught and translated into standardized RFC 7807 JSON responses or passed to your custom error handler.

---

## The Global Exception Boundary

When a route handler throws an exception or fails an assertion, Aegon intercepts it:

```
                  Client Request
                        │
                        ▼
               [ Router::dispatch() ]
                        │
                  Execute Handler
                 (co_await handler)
                        │
           ┌────────────┴────────────┐
       Success                     Throws Exception
           │                                 │
           ▼                                 ▼
   Send Response             [ Global Exception Boundary ]
                                             │
                            ┌────────────────┴────────────────┐
                            ▼                                 ▼
               Custom Error Handler?                Default Behavior
                        │                                     │
                 Execute Handler               Emit RFC 7807 500 Problem JSON
```

### Default Exception Handling (RFC 7807)

If no custom error handler is registered, any unhandled `std::exception` produces an `application/problem+json` response with HTTP 500:

```json
{
  "type": "about:blank",
  "title": "Internal Server Error",
  "status": 500,
  "detail": "Database connection timed out",
  "instance": "/api/users/123"
}
```

---

## Custom Global Error Handler

You can customize error mapping across your entire application by attaching an `ErrorHandler` to `server.set_error_handler()` or `router.set_error_handler()`:

```cpp
server.set_error_handler([](Context& ctx, std::exception_ptr ex) -> Task<void> {
    try {
        if (ex) std::rethrow_exception(ex);
    } 
    catch (const std::invalid_argument& e) {
        ctx.problem(
            StatusCode::BadRequest, 
            "Invalid Argument", 
            e.what()
        );
    } 
    catch (const std::runtime_error& e) {
        ctx.problem(
            StatusCode::InternalServerError, 
            "Runtime Error", 
            e.what()
        );
    } 
    catch (...) {
        ctx.problem(
            StatusCode::InternalServerError, 
            "Internal Error", 
            "An unexpected error occurred"
        );
    }
    co_return;
});
```

> [!TIP]
> If your custom error handler itself throws an exception, Aegon safely intercepts it and emits a standard RFC 7807 500 response, guaranteeing that the socket connection is never leaked.

---

## 404 Not Found & 405 Method Not Allowed

### Custom 404 Handler

To customize the response when a requested path does not exist:

```cpp
server.set_not_found_handler([](Context& ctx) -> Task<void> {
    ctx.problem(
        StatusCode::NotFound,
        "Resource Not Found",
        "The requested endpoint does not exist on this server."
    );
    co_return;
});
```

### Custom 405 Handler

When a path exists for another HTTP method (for example, `/orders` exists for `POST` but the client sent `DELETE`), Aegon triggers the `method_not_allowed_handler`:

```cpp
server.set_method_not_allowed_handler([](Context& ctx) -> Task<void> {
    ctx.problem(
        StatusCode::MethodNotAllowed,
        "Method Not Allowed",
        "The HTTP method is not permitted on this resource."
    );
    co_return;
});
```

---

## Using `ProblemDetails` Directly

In your handlers, you can emit standardized errors using `ctx.problem()`:

```cpp
server.router().post("/transfer", [](Context& ctx) -> Task<void> {
    bool has_sufficient_funds = false;

    if (!has_sufficient_funds) {
        ctx.problem(
            StatusCode::Forbidden,
            "Insufficient Funds",
            "Account balance ($12.50) is lower than requested transfer amount ($50.00)",
            "https://api.bank.com/errors/insufficient-funds"
        );
        co_return;
    }

    ctx.res().text("Transfer complete");
    co_return;
});
```

### Structure of `ProblemDetails`

```cpp
namespace aegon::http {
struct ProblemDetails {
    std::string type{"about:blank"}; // URI reference identifying problem type
    std::string title;               // Human-readable summary
    int status{500};                 // HTTP status code
    std::string detail;              // Human-readable explanation specific to occurrence
    std::string instance;            // URI reference identifying specific occurrence (e.g. req.path())
    
    std::string to_json() const;
};
}
```
