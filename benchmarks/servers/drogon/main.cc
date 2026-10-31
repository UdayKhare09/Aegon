#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace drogon;

namespace {

Json::Value gDataset(Json::arrayValue);

void loadDataset() {
    std::vector<std::string> datasetPaths = {
        "benchmarks/data/dataset.json",
        "data/dataset.json",
        "../data/dataset.json",
        "/data/dataset.json"
    };

    const char* envPath = std::getenv("DATASET_PATH");
    if (envPath) {
        datasetPaths.insert(datasetPaths.begin(), std::string(envPath));
    }

    for (const auto& path : datasetPaths) {
        if (std::filesystem::exists(path)) {
            std::ifstream input(path);
            if (!input) continue;
            Json::CharReaderBuilder builder;
            std::string errors;
            Json::Value parsed;
            if (Json::parseFromStream(builder, input, &parsed, &errors) && parsed.isArray()) {
                gDataset = parsed;
                std::cout << "[Drogon] Loaded " << gDataset.size() << " dataset items from " << path << "\n";
                return;
            }
        }
    }
    std::cerr << "[Drogon] Warning: No dataset file found. Using empty array.\n";
}

HttpResponsePtr plainText(const std::string& body) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_TEXT_PLAIN);
    resp->setBody(body);
    return resp;
}

HttpResponsePtr octetStream(std::string body) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->setBody(std::move(body));
    return resp;
}

long long toLong(const std::string& value) {
    try {
        return std::stoll(value);
    } catch (...) {
        return 0;
    }
}

} // namespace

// WebSocket Echo Controller
class WsEchoController : public drogon::WebSocketController<WsEchoController> {
public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr& wsConn,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override {
        wsConn->send(std::move(message), type);
    }

    void handleNewConnection(const drogon::HttpRequestPtr&,
                             const drogon::WebSocketConnectionPtr&) override {}

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr&) override {}

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws");
    WS_PATH_LIST_END
};

int main(int argc, char* argv[]) {
    int workers = 2;
    uint16_t basePort = 18095;
    std::string certFile = "benchmarks/certs/server.crt";
    std::string keyFile = "benchmarks/certs/server.key";

    if (argc > 1) {
        workers = std::stoi(argv[1]);
    }
    if (argc > 2) {
        basePort = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc > 3) {
        certFile = argv[3];
    }
    if (argc > 4) {
        keyFile = argv[4];
    }

    if (!std::filesystem::exists(certFile) || !std::filesystem::exists(keyFile)) {
        if (std::filesystem::exists("certs/server.crt") && std::filesystem::exists("certs/server.key")) {
            certFile = "certs/server.crt";
            keyFile = "certs/server.key";
        }
    }

    loadDataset();

    // 1. HttpArena Baseline (/baseline & /baseline11)
    auto baselineHandler = [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        long long sum = 0;
        const auto& params = req->getParameters();
        for (const auto& param : params) {
            sum += toLong(param.second);
        }
        if (req->method() == Post && !req->body().empty()) {
            sum += toLong(std::string(req->body()));
        }
        callback(plainText(std::to_string(sum)));
    };
    app().registerHandler("/baseline", baselineHandler, {Get, Post});
    app().registerHandler("/baseline11", baselineHandler, {Get, Post});

    // 2. HttpArena Pipeline (/pipeline)
    app().registerHandler(
        "/pipeline",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            callback(plainText("ok"));
        },
        {Get}
    );

    // 3. HttpArena Dynamic JSON from Dataset with Totals
    app().registerHandler(
        "/json/{count}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& count) {
            int items = static_cast<int>(toLong(count));
            if (items < 0) items = 0;
            if (items > static_cast<int>(gDataset.size())) items = static_cast<int>(gDataset.size());

            long long m = 1;
            const auto& multiplier = req->getParameter("m");
            if (!multiplier.empty()) {
                m = toLong(multiplier);
            }

            Json::Value list(Json::arrayValue);
            for (int i = 0; i < items; ++i) {
                Json::Value item = gDataset[i];
                item["total"] = static_cast<Json::Int64>(
                    item["price"].asInt64() * item["quantity"].asInt64() * m);
                list.append(std::move(item));
            }

            Json::Value payload;
            payload["items"] = std::move(list);
            payload["count"] = items;
            callback(HttpResponse::newHttpJsonResponse(payload));
        },
        {Get}
    );

    // 4. HttpArena Non-blocking Async Timer Delay (/delay/{ms})
    app().registerHandler(
        "/delay/{ms}",
        [](const HttpRequestPtr&,
           std::function<void(const HttpResponsePtr&)>&& callback,
           const std::string& ms) {
            const long long millis = toLong(ms);
            if (millis <= 0) {
                callback(plainText("0"));
                return;
            }
            app().getLoop()->runAfter(
                static_cast<double>(millis) / 1000.0,
                [callback, body = std::to_string(millis)]() { callback(plainText(body)); });
        },
        {Get}
    );

    // 5. HttpArena Stream Echo (/echo)
    app().registerHandler(
        "/echo",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            callback(octetStream(std::string(req->body())));
        },
        {Post}
    );

    // 6. HttpArena Static Files (/static/*)
    std::vector<std::string> staticPaths = {
        "benchmarks/data/static",
        "data/static",
        "../data/static",
        "/data/static"
    };
    for (const auto& path : staticPaths) {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            std::string absPath = std::filesystem::canonical(path).string();
            app().addALocation("/static", "", absPath);
            break;
        }
    }
    app().setStaticFilesCacheTime(-1);

    uint16_t portPlain = basePort;
    uint16_t portTls = basePort + 1;

    bool hasCerts = std::filesystem::exists(certFile) && std::filesystem::exists(keyFile);

    app().addListener("0.0.0.0", portPlain);
    if (hasCerts) {
        app().addListener("0.0.0.0", portTls, true, certFile, keyFile);
    }

    std::cout << "===================================================================\n";
    std::cout << " [Drogon Unified Server] Running with " << workers << " worker threads (HttpArena Spec)\n";
    std::cout << "   -> Plain HTTP/1.1 & WS : http://0.0.0.0:" << portPlain << "\n";
    if (hasCerts) {
        std::cout << "   -> TLS 1.3 HTTP/1.1    : https://0.0.0.0:" << portTls << "\n";
    }
    std::cout << "===================================================================\n" << std::flush;

    // Optimizations from HttpArena & Drogon best practices
    app().setLogLevel(trantor::Logger::kError)
        .setThreadNum(workers)
        .setClientMaxBodySize(30 * 1024 * 1024)
        .setClientMaxMemoryBodySize(30 * 1024 * 1024)
        .enableServerHeader(false)
        .enableDateHeader(true)
        .run();

    return 0;
}

