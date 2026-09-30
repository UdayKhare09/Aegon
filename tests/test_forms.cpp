#include "http/Server.h"
#include "http/MultipartParser.h"
#include "data/validation/Validator.h"
#include <iostream>
#include <cassert>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::http;

// ---------------------------------------------------------------------------
// DTOs for bind_form testing
// ---------------------------------------------------------------------------
struct UserRegistrationForm {
    std::string username;
    std::string email;
    int age{0};
    bool terms_accepted{false};

    void validate(aegon::validation::ValidationRules& v) const {
        v.field("username", username).min_len(3).max_len(20);
        v.field("email", email).email();
        v.field("age", age).min(18);
        if (!terms_accepted) {
            v.add_violation("terms_accepted", "Must accept terms");
        }
    }
};

static std::string exec_cmd(const std::string& cmd) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "";
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        out += buf;
    }
    pclose(fp);
    return out;
}

// ---------------------------------------------------------------------------
// 1. Unit Tests: application/x-www-form-urlencoded
// ---------------------------------------------------------------------------
void test_urlencoded_unit() {
    std::cout << "[UNIT TEST] Testing application/x-www-form-urlencoded parsing... " << std::flush;

    Request req;
    req.set_body("username=john_doe&email=john%40example.com&tag=c%2B%2B26&empty_val=&active=true&score=98.5");

    // 1. Direct form() access
    assert(req.form("username") == "john_doe");
    assert(req.form("email") == "john@example.com");
    assert(req.form("tag") == "c++26");
    assert(req.form("empty_val") == "");
    assert(req.form("active") == "true");
    assert(req.form("score") == "98.5");
    assert(!req.form("non_existent").has_value());

    // 2. bind_form<T>() with valid data
    {
        Request valid_req;
        valid_req.set_body("username=udaykhare&email=uday%40example.com&age=25&terms_accepted=true");
        Response res;
        auto form_dto = valid_req.bind_form<UserRegistrationForm>(res);
        assert(form_dto.has_value());
        assert(form_dto->username == "udaykhare");
        assert(form_dto->email == "uday@example.com");
        assert(form_dto->age == 25);
        assert(form_dto->terms_accepted == true);
    }

    // 3. bind_form<T>() with validation failure (age < 18, invalid email)
    {
        Request invalid_req;
        invalid_req.set_body("username=ab&email=not_an_email&age=15&terms_accepted=false");
        Response res;
        auto form_dto = invalid_req.bind_form<UserRegistrationForm>(res);
        assert(!form_dto.has_value());
        assert(res.status() == StatusCode::UnprocessableEntity);
        assert(res.body().find("violations") != std::string::npos);
        assert(res.body().find("username") != std::string::npos);
        assert(res.body().find("email") != std::string::npos);
        assert(res.body().find("age") != std::string::npos);
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. Unit Tests: multipart/form-data
// ---------------------------------------------------------------------------
void test_multipart_unit() {
    std::cout << "[UNIT TEST] Testing multipart/form-data parsing & file extraction... " << std::flush;

    // 1. Boundary extraction (unquoted & quoted)
    assert(MultipartParser::extract_boundary("multipart/form-data; boundary=----WebKitFormBoundary123") == "----WebKitFormBoundary123");
    assert(MultipartParser::extract_boundary("multipart/form-data; boundary=\"----MyQuotedBoundary\"") == "----MyQuotedBoundary");
    assert(MultipartParser::extract_boundary("multipart/form-data; boundary=xyz; charset=utf-8") == "xyz");

    // 2. Full multipart body parsing
    std::string boundary = "----AegonTestBoundary7MA4YWxkTrZu0gW";
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    std::string body;
    body.append("--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"title\"\r\n"
        "\r\n"
        "Release Notes v1.0\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"author\"\r\n"
        "\r\n"
        "Aegon Core Team\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"document\"; filename=\"release.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Aegon C++26 high-performance asynchronous server.\nBuilt on io_uring.\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"binary_data\"; filename=\"raw.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n");
    body.append("\x00\x01\x02\xFF\xFE\xFD", 6);
    body.append("\r\n--" + boundary + "--\r\n");

    auto form_opt = MultipartParser::parse(content_type, body);
    assert(form_opt.has_value());
    const auto& form = *form_opt;

    // Verify fields
    assert(form.get("title") == "Release Notes v1.0");
    assert(form.get("author") == "Aegon Core Team");
    assert(!form.get("non_existent").has_value());

    // Verify files
    auto doc_opt = form.file("document");
    assert(doc_opt.has_value());
    assert(doc_opt->filename == "release.txt");
    assert(doc_opt->content_type == "text/plain");
    assert(doc_opt->data == "Aegon C++26 high-performance asynchronous server.\nBuilt on io_uring.");

    auto bin_opt = form.file("binary_data");
    assert(bin_opt.has_value());
    assert(bin_opt->filename == "raw.bin");
    assert(bin_opt->content_type == "application/octet-stream");
    assert(bin_opt->size() == 6);
    assert(bin_opt->data[0] == '\x00');
    assert(bin_opt->data[3] == '\xFF');

    // Test save_to helper
    std::string temp_out = "/tmp/aegon_test_save.txt";
    assert(doc_opt->save_to(temp_out));
    std::ifstream ifs(temp_out, std::ios::binary);
    std::string read_back((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    assert(read_back == doc_opt->data);
    std::filesystem::remove(temp_out);

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 3. Live Server Integration Tests
// ---------------------------------------------------------------------------
void test_live_forms_server() {
    std::cout << "[LIVE SERVER] Testing form submissions over loopback socket... " << std::flush;

    constexpr uint16_t PORT = 19885;
    Router router;

    // URL-encoded endpoint
    router.post("/api/register", [](Context& ctx) {
        auto form = ctx.bind_form<UserRegistrationForm>();
        if (!form) return; // 400 or 422 was already set by bind_form
        ctx.res().status(StatusCode::Created).json(R"({"registered":")" + form->username + R"("})");
    });

    // Multipart endpoint
    router.post("/api/upload", [](Context& ctx) {
        auto form = ctx.multipart();
        if (!form) {
            ctx.res().status(StatusCode::BadRequest).text("Malformed multipart body");
            return;
        }

        auto tag = form->get("tag").value_or("default");
        auto file = form->file("attachment");
        if (!file) {
            ctx.res().status(StatusCode::BadRequest).text("Missing attachment file");
            return;
        }

        std::string json_reply = "{\"tag\":\"" + std::string(tag) + "\",\"filename\":\"" +
                                 std::string(file->filename) + "\",\"bytes\":" +
                                 std::to_string(file->size()) + "}";
        ctx.res().status(StatusCode::Ok).json(json_reply);
    });

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 1. Test URL-encoded form success via curl
    {
        std::string cmd = "curl -s -X POST http://127.0.0.1:" + std::to_string(PORT) +
                          "/api/register -d 'username=sarah_connor&email=sarah%40skynet.com&age=29&terms_accepted=true'";
        std::string resp = exec_cmd(cmd);
        assert(resp.find(R"("registered":"sarah_connor")") != std::string::npos);
    }

    // 2. Test URL-encoded form validation failure (returns 422)
    {
        std::string cmd = "curl -s -i -X POST http://127.0.0.1:" + std::to_string(PORT) +
                          "/api/register -d 'username=x&email=bad_email&age=12&terms_accepted=false'";
        std::string resp = exec_cmd(cmd);
        assert(resp.find("422 Unprocessable Entity") != std::string::npos);
        assert(resp.find("violations") != std::string::npos);
    }

    // 3. Test Multipart file & field upload via curl -F
    {
        // Create a temporary file to upload
        std::string temp_upload = "/tmp/aegon_test_upload.dat";
        {
            std::ofstream ofs(temp_upload, std::ios::binary);
            ofs << "Test payload bytes for multipart upload in Aegon C++26!";
        }

        std::string cmd = "curl -s -X POST http://127.0.0.1:" + std::to_string(PORT) +
                          "/api/upload -F 'tag=v1_assets' -F 'attachment=@" + temp_upload + "'";
        std::string resp = exec_cmd(cmd);

        assert(resp.find(R"("tag":"v1_assets")") != std::string::npos);
        assert(resp.find(R"("filename":"aegon_test_upload.dat")") != std::string::npos);
        assert(resp.find(R"("bytes":55)") != std::string::npos);

        std::filesystem::remove(temp_upload);
    }

    server.stop();
    // Wake up accept loop
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    connect(fd, (sockaddr*)&a, sizeof(a));
    close(fd);

    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "       AEGON FORM PARSER & DTO BINDING TEST SUITE      \n";
    std::cout << "    application/x-www-form-urlencoded & multipart       \n";
    std::cout << "=======================================================\n\n";

    test_urlencoded_unit();
    test_multipart_unit();
    test_live_forms_server();

    std::cout << "\n=======================================================\n";
    std::cout << "ALL FORM PARSER TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=======================================================\n";
    return 0;
}
