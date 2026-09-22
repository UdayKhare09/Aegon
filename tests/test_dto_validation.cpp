#include "http/Context.h"
#include "http/Request.h"
#include "http/Response.h"
#include "http/Router.h"
#include "data/validation/Validator.h"
#include "data/types/UUID.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <optional>

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace aegon;
using namespace aegon::http;
using namespace aegon::validation;

// 1. Nested Address DTO
struct AddressDTO {
    std::string street;
    std::string city;
    std::string zip_code;

    void validate(ValidationRules& v) const {
        v.field("street", street).required();
        v.field("city", city).required();
        v.field("zip_code", zip_code).required().min_len(5);
    }
};

// 2. Parent User DTO with nested objects, optional object, and list of objects
struct CreateUserDTO {
    std::string username;
    std::string email;
    int age{0};
    AddressDTO address;
    std::optional<AddressDTO> shipping;
    std::vector<AddressDTO> backup_addresses;

    void validate(ValidationRules& v) const {
        v.field("username", username).required().min_len(3).max_len(30);
        v.field("email", email).required().email();
        v.field("age", age).min(18).max(120);

        v.nested("address", address);
        v.nested("shipping", shipping);
        v.nested_each("backup_addresses", backup_addresses);
    }
};

// 3. Outbound Response DTO
struct UserResponseDTO {
    std::string id;
    std::string username;
    std::string email;
    AddressDTO address;
    std::vector<std::string> tags;
};

// 4. Query Params DTO
struct SearchQueryDTO {
    std::string q;
    int page{1};
    int limit{20};
    bool active{true};

    void validate(ValidationRules& v) const {
        v.field("q", q).required();
        v.field("limit", limit).min(1).max(100);
    }
};

// 5. Route Path Params DTO
struct UserPostPathDTO {
    int user_id{0};
    int post_id{0};

    void validate(ValidationRules& v) const {
        v.field("user_id", user_id).min(1);
        v.field("post_id", post_id).min(1);
    }
};

// 6. UUID Entity DTO with UUID, optional<UUID>, and vector<UUID>
struct AccountDTO {
    data::UUID id;
    std::string name;
    std::optional<data::UUID> tenant_id;
    std::vector<data::UUID> role_ids;

    void validate(ValidationRules& v) const {
        v.field("name", name).required();
        v.field("id", id).not_nil("Account id cannot be nil");
        v.field("tenant_id", tenant_id).not_nil("Tenant id cannot be nil");
    }
};

void test_dto_json_binding_success() {
    std::cout << "[Test 1] Testing successful JSON DTO binding with nested objects...\n";

    Request req;
    req.set_method(Method::POST);
    req.set_path("/users");
    req.set_body(R"({
        "username": "uday",
        "email": "uday@example.com",
        "age": 28,
        "address": {
            "street": "100 Innovation Way",
            "city": "Bengaluru",
            "zip_code": "560001"
        },
        "shipping": {
            "street": "200 Shipping Blvd",
            "city": "Bengaluru",
            "zip_code": "560002"
        },
        "backup_addresses": [
            {
                "street": "300 Backup Rd",
                "city": "Bengaluru",
                "zip_code": "560003"
            }
        ]
    })");

    Response res;
    Context ctx(req, res);

    auto dto = ctx.bind_json<CreateUserDTO>();
    TEST_CHECK(dto.has_value());
    TEST_CHECK(dto->username == "uday");
    TEST_CHECK(dto->email == "uday@example.com");
    TEST_CHECK(dto->age == 28);
    TEST_CHECK(dto->address.street == "100 Innovation Way");
    TEST_CHECK(dto->address.city == "Bengaluru");
    TEST_CHECK(dto->address.zip_code == "560001");
    TEST_CHECK(dto->shipping.has_value());
    TEST_CHECK(dto->shipping->street == "200 Shipping Blvd");
    TEST_CHECK(dto->backup_addresses.size() == 1);
    TEST_CHECK(dto->backup_addresses[0].street == "300 Backup Rd");

    std::cout << "  -> PASS: Inbound nested JSON parsed and validated with zero errors.\n";
}

void test_dto_validation_nested_failures() {
    std::cout << "[Test 2] Testing auto-rejection & Spring-style RFC 7807 422 JSON errors...\n";

    Request req;
    req.set_method(Method::POST);
    req.set_path("/users");
    // Invalid username (too short), invalid email, age < 18, and invalid nested address zip_code
    req.set_body(R"({
        "username": "u",
        "email": "not-an-email",
        "age": 15,
        "address": {
            "street": "100 Main St",
            "city": "Metropolis",
            "zip_code": "12"
        },
        "backup_addresses": [
            {
                "street": "",
                "city": "Gotham",
                "zip_code": "12345"
            }
        ]
    })");

    Response res;
    Context ctx(req, res);

    auto dto = ctx.bind_json<CreateUserDTO>();
    TEST_CHECK(!dto.has_value()); // Automatically rejected!
    TEST_CHECK(res.status() == StatusCode::UnprocessableEntity);

    std::string err_body = std::string(res.body());
    TEST_CHECK(err_body.find("\"status\":422") != std::string::npos);
    TEST_CHECK(err_body.find("\"username\"") != std::string::npos);
    TEST_CHECK(err_body.find("\"email\"") != std::string::npos);
    TEST_CHECK(err_body.find("\"age\"") != std::string::npos);
    // Verify nested error paths
    TEST_CHECK(err_body.find("\"address.zip_code\"") != std::string::npos);
    TEST_CHECK(err_body.find("\"backup_addresses[0].street\"") != std::string::npos);

    std::cout << "  -> PASS: 422 Unprocessable Entity error report produced with exact nested paths.\n";
}

void test_dto_malformed_json_syntax() {
    std::cout << "[Test 3] Testing malformed JSON syntax auto-rejection (400 Bad Request)...\n";

    Request req;
    req.set_method(Method::POST);
    req.set_body(R"({ "username": "broken" ... )"); // Syntax error

    Response res;
    Context ctx(req, res);

    auto dto = ctx.bind_json<CreateUserDTO>();
    TEST_CHECK(!dto.has_value());
    TEST_CHECK(res.status() == StatusCode::BadRequest);
    TEST_CHECK(res.body().find("\"status\":400") != std::string::npos);
    TEST_CHECK(res.body().find("Malformed JSON") != std::string::npos);

    std::cout << "  -> PASS: Malformed JSON automatically returns 400 Bad Request.\n";
}

void test_dto_query_binding() {
    std::cout << "[Test 4] Testing URL query string DTO binding with validation...\n";

    // Valid query string
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/search");
        req.set_query("q=cybernetics&page=2&limit=50&active=true");

        Response res;
        Context ctx(req, res);

        auto query_dto = ctx.bind_query<SearchQueryDTO>();
        TEST_CHECK(query_dto.has_value());
        TEST_CHECK(query_dto->q == "cybernetics");
        TEST_CHECK(query_dto->page == 2);
        TEST_CHECK(query_dto->limit == 50);
        TEST_CHECK(query_dto->active == true);
    }

    // Invalid query string (limit exceeds max 100)
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/search");
        req.set_query("q=cybernetics&limit=500");

        Response res;
        Context ctx(req, res);

        auto query_dto = ctx.bind_query<SearchQueryDTO>();
        TEST_CHECK(!query_dto.has_value());
        TEST_CHECK(res.status() == StatusCode::UnprocessableEntity);
        TEST_CHECK(res.body().find("\"limit\"") != std::string::npos);
    }

    std::cout << "  -> PASS: Query string successfully bound and validated into DTO.\n";
}

void test_dto_path_binding() {
    std::cout << "[Test 5] Testing route path parameter DTO binding with validation...\n";

    // Valid path params
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/users/42/posts/101");
        req.add_param("user_id", "42");
        req.add_param("post_id", "101");

        Response res;
        Context ctx(req, res);

        auto path_dto = ctx.bind_path<UserPostPathDTO>();
        TEST_CHECK(path_dto.has_value());
        TEST_CHECK(path_dto->user_id == 42);
        TEST_CHECK(path_dto->post_id == 101);
    }

    // Invalid path param (user_id = 0 violates min(1))
    {
        Request req;
        req.set_method(Method::GET);
        req.set_path("/users/0/posts/101");
        req.add_param("user_id", "0");
        req.add_param("post_id", "101");

        Response res;
        Context ctx(req, res);

        auto path_dto = ctx.bind_path<UserPostPathDTO>();
        TEST_CHECK(!path_dto.has_value());
        TEST_CHECK(res.status() == StatusCode::UnprocessableEntity);
        TEST_CHECK(res.body().find("\"user_id\"") != std::string::npos);
    }

    std::cout << "  -> PASS: Route path parameters successfully bound and validated into DTO.\n";
}

void test_outbound_dto_serialization() {
    std::cout << "[Test 6] Testing outbound DTO serialization via ctx.res().json(dto)...\n";

    UserResponseDTO response{
        .id = "USR-9988",
        .username = "uday",
        .email = "uday@aegon.dev",
        .address = {
            .street = "Tech Park 4",
            .city = "Bengaluru",
            .zip_code = "560100"
        },
        .tags = {"admin", "developer", "vip"}
    };

    Response res;
    res.status(StatusCode::Created).json(response);

    TEST_CHECK(res.status() == StatusCode::Created);
    TEST_CHECK(res.headers().get("Content-Type").value_or("").starts_with("application/json"));

    std::string json_out = std::string(res.body());
    TEST_CHECK(json_out.find("\"id\":\"USR-9988\"") != std::string::npos);
    TEST_CHECK(json_out.find("\"username\":\"uday\"") != std::string::npos);
    TEST_CHECK(json_out.find("\"street\":\"Tech Park 4\"") != std::string::npos);
    TEST_CHECK(json_out.find("\"admin\"") != std::string::npos);

    std::cout << "  -> PASS: Outbound DTO with nested struct and vector serialized cleanly.\n";
}

void test_dto_uuid_binding_and_serialization() {
    std::cout << "[Test 7] Testing UUID DTO binding and serialization (UUID, optional<UUID>, vector<UUID>)...\n";

    Request req;
    req.set_method(Method::POST);
    req.set_path("/accounts");
    req.set_body(R"({
        "id": "f47ac10b-58cc-4372-a567-0e02b2c3d479",
        "name": "Production Org",
        "tenant_id": "c9a646d3-9c61-4cd7-bf5c-ff189fc48281",
        "role_ids": [
            "a0000000-0000-0000-0000-000000000001",
            "a0000000-0000-0000-0000-000000000002"
        ]
    })");

    Response res;
    Context ctx(req, res);

    auto dto = ctx.bind_json<AccountDTO>();
    TEST_CHECK(dto.has_value());
    TEST_CHECK(dto->id.to_string() == "f47ac10b-58cc-4372-a567-0e02b2c3d479");
    TEST_CHECK(dto->name == "Production Org");
    TEST_CHECK(dto->tenant_id.has_value());
    TEST_CHECK(dto->tenant_id->to_string() == "c9a646d3-9c61-4cd7-bf5c-ff189fc48281");
    TEST_CHECK(dto->role_ids.size() == 2);
    TEST_CHECK(dto->role_ids[0].to_string() == "a0000000-0000-0000-0000-000000000001");
    TEST_CHECK(dto->role_ids[1].to_string() == "a0000000-0000-0000-0000-000000000002");

    // Outbound serialization of the parsed DTO
    Response out_res;
    out_res.status(StatusCode::Ok).json(*dto);
    TEST_CHECK(out_res.status() == StatusCode::Ok);
    std::string out_body = std::string(out_res.body());
    TEST_CHECK(out_body.find("\"id\":\"f47ac10b-58cc-4372-a567-0e02b2c3d479\"") != std::string::npos);
    TEST_CHECK(out_body.find("\"tenant_id\":\"c9a646d3-9c61-4cd7-bf5c-ff189fc48281\"") != std::string::npos);
    TEST_CHECK(out_body.find("\"a0000000-0000-0000-0000-000000000001\"") != std::string::npos);
    TEST_CHECK(out_body.find("\"a0000000-0000-0000-0000-000000000002\"") != std::string::npos);

    std::cout << "  -> PASS: UUID, optional<UUID>, and vector<UUID> bound and serialized seamlessly.\n";
}

void test_dto_uuid_validation_and_syntax_error() {
    std::cout << "[Test 8] Testing UUID syntax auto-rejection (400) and nil UUID validation (422)...\n";

    // 1. Malformed UUID string in JSON -> auto 400 Bad Request
    {
        Request req;
        req.set_method(Method::POST);
        req.set_body(R"({
            "id": "not-a-valid-uuid-format",
            "name": "Acme Corp"
        })");

        Response res;
        Context ctx(req, res);

        auto dto = ctx.bind_json<AccountDTO>();
        TEST_CHECK(!dto.has_value());
        TEST_CHECK(res.status() == StatusCode::BadRequest);
        TEST_CHECK(res.body().find("Malformed JSON") != std::string::npos);
    }

    // 2. Nil UUID violating .not_nil() validator -> auto 422 Unprocessable Entity
    {
        Request req;
        req.set_method(Method::POST);
        req.set_body(R"({
            "id": "00000000-0000-0000-0000-000000000000",
            "name": "Acme Corp",
            "tenant_id": "00000000-0000-0000-0000-000000000000"
        })");

        Response res;
        Context ctx(req, res);

        auto dto = ctx.bind_json<AccountDTO>();
        TEST_CHECK(!dto.has_value());
        TEST_CHECK(res.status() == StatusCode::UnprocessableEntity);
        TEST_CHECK(res.body().find("\"id\"") != std::string::npos);
        TEST_CHECK(res.body().find("Account id cannot be nil") != std::string::npos);
        TEST_CHECK(res.body().find("\"tenant_id\"") != std::string::npos);
        TEST_CHECK(res.body().find("Tenant id cannot be nil") != std::string::npos);
    }

    std::cout << "  -> PASS: Invalid UUID string correctly yields 400, and nil UUID yields 422.\n";
}

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "     AEGON C++26 DTO BINDING & VALIDATION TEST SUITE    \n";
    std::cout << "=======================================================\n\n";

    test_dto_json_binding_success();
    test_dto_validation_nested_failures();
    test_dto_malformed_json_syntax();
    test_dto_query_binding();
    test_dto_path_binding();
    test_outbound_dto_serialization();
    test_dto_uuid_binding_and_serialization();
    test_dto_uuid_validation_and_syntax_error();

    std::cout << "\n=======================================================\n";
    std::cout << "   >>> ALL DTO VALIDATION TESTS PASSED SUCCESSFULLY! <<<\n";
    std::cout << "=======================================================\n\n";
    return 0;
}

