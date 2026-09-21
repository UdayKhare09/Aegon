#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <iostream>
#include <chrono>
#include <cstdlib>

using namespace drogon;

int main(int argc, char* argv[]) {
    size_t threads = 4;
    uint16_t port = 18083;

    if (argc > 1) {
        threads = static_cast<size_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    // 1. Plaintext benchmark (TechEmpower standard)
    app().registerHandler("/plaintext", [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody("Hello, World!");
        callback(resp);
    }, {Get});

    // 2. JSON Serialization benchmark with dynamic UUID & timestamp
    app().registerHandler("/json", [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value json;
        json["message"] = "Hello, World!";
        json["id"] = drogon::utils::getUuid();
        json["timestamp"] = static_cast<Json::UInt64>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    }, {Get});

    // 3. Dynamic route with two URL path parameters
    app().registerHandler("/users/{id}/posts/{post_id}", [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id, const std::string& post_id) {
        Json::Value json;
        json["user_id"] = static_cast<Json::UInt64>(std::stoull(id));
        json["post_id"] = static_cast<Json::UInt64>(std::stoull(post_id));
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    }, {Get});

    std::cout << "Drogon listening on http://0.0.0.0:" << port << " with " << threads << " threads" << std::endl;
    app().setLogPath("")
         .setLogLevel(trantor::Logger::kWarn)
         .addListener("0.0.0.0", port)
         .setThreadNum(threads)
         .run();

    return 0;
}
