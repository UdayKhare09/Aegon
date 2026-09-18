# Data Validation & DTOs

Aegon includes a compile-time declarative validation engine (`aegon::validation::ValidationRules`). 

Any struct or class that implements a `validate(ValidationRules& v) const` member function is recognized by the `HasValidate<T>` C++20 concept. When using `ctx.bind_json<T>()`, `ctx.bind_query<T>()`, or `ctx.bind_path<T>()`, validation is executed automatically, emitting an RFC-compliant `422 Unprocessable Entity` response upon failure.

---

## Defining a Validated DTO

Implement the `validate` method on your DTO:

```cpp
#include <aegon/data/validation/Validator.h>

struct RegisterUserDto {
    std::string username;
    std::string email;
    int age;
    std::optional<std::string> referral_code;

    void validate(aegon::validation::ValidationRules& v) const {
        v.field("username", username)
            .required()
            .min_len(3)
            .max_len(30);

        v.field("email", email)
            .required()
            .email();

        v.field("age", age)
            .min(18)
            .max(120);

        v.field("referral_code", referral_code)
            .min_len(6); // Only checked if referral_code.has_value()
    }
};
```

---

## Automatic 422 Handling

When called within a route handler:

```cpp
server.router().post("/api/register", [](Context& ctx) -> Task<void> {
    auto dto = ctx.bind_json<RegisterUserDto>();
    if (!dto) {
        // If validation fails, bind_json() has already set status 422 
        // and populated the JSON violation report on ctx.res()!
        co_return;
    }

    // Proceeds only if payload is valid!
    ctx.res().status(StatusCode::Created).text("Registration successful");
    co_return;
});
```

### JSON Violation Report Format

```json
{
  "status": 422,
  "error": "Unprocessable Entity",
  "message": "Validation failed",
  "violations": {
    "username": "Length must be at least 3",
    "email": "Must be a valid email address",
    "age": "Value must be at least 18"
  }
}
```

---

## Built-In Validation Rules

The `FieldValidator<T>` returned by `v.field(name, value)` provides fluent chaining:

| Rule | Supported Types | Description |
| :--- | :--- | :--- |
| `.required(msg)` | `std::string`, `std::vector`, `std::optional` | Ensures string/vector is non-empty, or optional has a value. |
| `.min_len(N, msg)` | `std::string`, `std::optional<std::string>` | String length must be $\ge N$. |
| `.max_len(N, msg)` | `std::string`, `std::optional<std::string>` | String length must be $\le N$. |
| `.email(msg)` | `std::string`, `std::optional<std::string>` | Validates standard email address formatting. |
| `.min(val, msg)` | Numeric types, `std::optional<Num>` | Value must be $\ge \text{val}$. |
| `.max(val, msg)` | Numeric types, `std::optional<Num>` | Value must be $\le \text{val}$. |
| `.positive(msg)` | Numeric types, `std::optional<Num>` | Value must be $> 0$. |
| `.not_nil(msg)` | Types with `.is_nil()` (e.g. `UUID`) | Ensures identifier is not nil (`00000000-...`). |
| `.past(msg)` | Timestamp types | Validates timestamp is in the past. |
| `.future(msg)` | Timestamp types | Validates timestamp is in the future. |
| `.custom(pred, msg)`| Any type | Accepts custom lambda `[](const auto& val) -> bool`. |

### Custom Predicate Example

```cpp
v.field("slug", slug).custom([](std::string_view s) {
    return s.find(' ') == std::string_view::npos;
}, "Slug cannot contain whitespace");
```

---

## Nested & Array Validation

Real-world APIs frequently deal with hierarchical structures (e.g., an Order containing an Address and a list of Line Items). Aegon supports automatic cascading validation with dot-notation and indexed paths.

```cpp
struct AddressDto {
    std::string street;
    std::string zip;

    void validate(aegon::validation::ValidationRules& v) const {
        v.field("street", street).required();
        v.field("zip", zip).required().min_len(5);
    }
};

struct OrderItemDto {
    std::string sku;
    int quantity;

    void validate(aegon::validation::ValidationRules& v) const {
        v.field("sku", sku).required();
        v.field("quantity", quantity).positive();
    }
};

struct CreateOrderDto {
    AddressDto shipping_address;
    std::optional<AddressDto> billing_address;
    std::vector<OrderItemDto> items;

    void validate(aegon::validation::ValidationRules& v) const {
        // 1. Nested object -> prefixes violations with "shipping_address."
        v.nested("shipping_address", shipping_address);

        // 2. Optional nested object -> validated only if present
        v.nested("billing_address", billing_address);

        // 3. Array of nested objects -> prefixes violations with "items[0].", "items[1].", etc.
        v.nested_each("items", items);
    }
};
```

If an item in the array has an invalid quantity, the resulting violation map accurately reflects its array position:

```json
{
  "status": 422,
  "error": "Unprocessable Entity",
  "message": "Validation failed",
  "violations": {
    "shipping_address.zip": "Length must be at least 5",
    "items[1].quantity": "Must be positive"
  }
}
```

---

## Standalone Manual Validation

You can also use `ValidationRules` independently outside of HTTP controllers:

```cpp
aegon::validation::ValidationRules v;
user.validate(v);

if (v.has_violations()) {
    std::string json_report = v.to_json();
    for (const auto& [field, message] : v.violations()) {
        std::cerr << field << ": " << message << "\n";
    }
}
```
