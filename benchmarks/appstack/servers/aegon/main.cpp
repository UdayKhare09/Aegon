#include <aegon/http/Server.h>
#include <aegon/core/Task.h>
#include <aegon/core/EventLoop.h>
#include <aegon/http/middleware/Compress.h>
#include <glaze/glaze.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <charconv>
#include <cctype>
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <array>
#include <deque>
#include <coroutine>

using namespace aegon::http;

// ── Profile 1: Baseline routes ──────────────────────────────────────────────

void handle_baseline_get(Context& ctx) {
    int64_t sum = 0;
    std::string_view q = ctx.req().query();
    while (!q.empty()) {
        size_t amp = q.find('&');
        std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            int64_t val = 0;
            std::from_chars(pair.data() + eq + 1, pair.data() + pair.size(), val);
            sum += val;
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), sum);
    ctx.res().text(std::string_view(buf, p - buf));
}

void handle_baseline_post(Context& ctx) {
    int64_t sum = 0;
    std::string_view q = ctx.req().query();
    while (!q.empty()) {
        size_t amp = q.find('&');
        std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            int64_t val = 0;
            std::from_chars(pair.data() + eq + 1, pair.data() + pair.size(), val);
            sum += val;
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }
    std::string_view body = ctx.req().body();
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front()))) body.remove_prefix(1);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back()))) body.remove_suffix(1);
    if (!body.empty()) {
        int64_t body_val = 0;
        auto [ptr, ec] = std::from_chars(body.data(), body.data() + body.size(), body_val);
        if (ec == std::errc()) {
            sum += body_val;
        }
    }
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), sum);
    ctx.res().text(std::string_view(buf, p - buf));
}

// ── Profile 2: Async delay route ────────────────────────────────────────────

aegon::core::Task<void> handle_delay(Context& ctx) {
    uint64_t ms = 0;
    if (auto ms_str = ctx.req().param("ms")) {
        std::from_chars(ms_str->data(), ms_str->data() + ms_str->size(), ms);
    }
    if (ms > 0) {
        co_await aegon::core::EventLoop::current()->ring().timeout(ms * 1'000'000ULL);
    }
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), ms);
    ctx.res().text(std::string_view(buf, p - buf));
}

// ── Profile 3: JSON Serialization & Compression ─────────────────────────────

struct Rating {
    int64_t score{0};
    int64_t count{0};
};

struct DatasetItem {
    int64_t id{0};
    std::string name;
    std::string category;
    int64_t price{0};
    int64_t quantity{0};
    bool active{false};
    std::vector<std::string> tags;
    Rating rating;
};

// Zero-copy view into dataset item with per-request total calculation
struct ProcessedItem {
    int64_t id{0};
    std::string_view name;
    std::string_view category;
    int64_t price{0};
    int64_t quantity{0};
    bool active{false};
    std::span<const std::string> tags;
    Rating rating;
    int64_t total{0};
};

struct JsonResponsePayload {
    std::span<const ProcessedItem> items;
    size_t count{0};
};

static std::vector<DatasetItem> g_dataset;
inline thread_local std::array<ProcessedItem, 64> tls_processed_items;

static void load_dataset() {
    const char* env_path = std::getenv("DATASET_PATH");
    std::vector<std::string> candidate_paths;
    if (env_path) candidate_paths.push_back(env_path);
    candidate_paths.push_back("data/dataset.json");
    candidate_paths.push_back("benchmarks/appstack/data/dataset.json");
    candidate_paths.push_back("../data/dataset.json");
    candidate_paths.push_back("../../data/dataset.json");

    std::string content;
    for (const auto& path : candidate_paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            content = ss.str();
            std::cout << "[Aegon] Loaded dataset from " << path << " (" << content.size() << " bytes)" << std::endl;
            break;
        }
    }

    if (content.empty()) {
        std::cerr << "[Aegon] WARNING: Could not find dataset.json!" << std::endl;
        return;
    }

    auto err = glz::read_json(g_dataset, content);
    if (err) {
        std::cerr << "[Aegon] ERROR parsing dataset.json: " << glz::format_error(err, content) << std::endl;
    } else {
        std::cout << "[Aegon] Parsed " << g_dataset.size() << " items from dataset.json" << std::endl;
    }
}

void handle_json(Context& ctx) {
    size_t count = 50;
    if (auto c_str = ctx.req().param("count")) {
        std::from_chars(c_str->data(), c_str->data() + c_str->size(), count);
    }
    count = std::min(count, std::min(g_dataset.size(), tls_processed_items.size()));

    int64_t m = 1;
    if (auto m_val = ctx.req().query_param("m")) {
        std::from_chars(m_val->data(), m_val->data() + m_val->size(), m);
    }

    for (size_t i = 0; i < count; ++i) {
        const auto& d = g_dataset[i];
        tls_processed_items[i] = ProcessedItem{
            .id = d.id,
            .name = d.name,
            .category = d.category,
            .price = d.price,
            .quantity = d.quantity,
            .active = d.active,
            .tags = std::span<const std::string>(d.tags.data(), d.tags.size()),
            .rating = d.rating,
            .total = d.price * d.quantity * m
        };
    }

    JsonResponsePayload payload{
        .items = std::span<const ProcessedItem>(tls_processed_items.data(), count),
        .count = count
    };

    ctx.res().status(StatusCode::Ok).json(payload);
}

// ── Profile 4: Async PostgreSQL database ────────────────────────────────────

#include <libpq-fe.h>
#include <cstring>
#include <poll.h>

struct DbRating {
    int score{0};
    int64_t count{0};
};

struct DbItem {
    int64_t id{0};
    std::string_view name;
    std::string_view category;
    int price{0};
    int quantity{0};
    bool active{false};
    glz::raw_json tags;
    DbRating rating;
};

struct DbResponse {
    std::span<const DbItem> items;
    size_t count{0};
};

struct PipelineSlot {
    PGconn* conn{nullptr};
    int fd{-1};
    std::deque<std::pair<std::coroutine_handle<>, PGresult**>> in_flight;
};

struct PipelineAwaiter {
    PipelineSlot& slot;
    PGresult* res{nullptr};

    explicit PipelineAwaiter(PipelineSlot& s) noexcept : slot(s) {}

    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        slot.in_flight.push_back({h, &res});
    }
    PGresult* await_resume() noexcept { return res; }
};

struct PgPipelinePool {
    std::vector<std::unique_ptr<PipelineSlot>> slots;
    std::string conninfo;

    explicit PgPipelinePool(std::string info, size_t num_slots = 4)
        : conninfo(std::move(info)) {
        for (size_t i = 0; i < num_slots; ++i) {
            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (!conn || PQstatus(conn) != CONNECTION_OK) {
                if (conn) PQfinish(conn);
                continue;
            }
            PQsetnonblocking(conn, 1);
            PGresult* prep = PQexec(conn,
                "PREPARE stmt_async_db AS "
                "SELECT id, name, category, price, quantity, active, tags, rating_score, rating_count "
                "FROM items WHERE price BETWEEN $1 AND $2 LIMIT $3");
            PQclear(prep);

            if (PQenterPipelineMode(conn) != 1) {
                PQfinish(conn);
                continue;
            }

            auto slot = std::make_unique<PipelineSlot>();
            slot->conn = conn;
            slot->fd = PQsocket(conn);
            slots.push_back(std::move(slot));
        }
    }

    PipelineSlot* pick_slot() noexcept {
        if (slots.empty()) return nullptr;
        PipelineSlot* best = slots[0].get();
        for (size_t i = 1; i < slots.size(); ++i) {
            if (slots[i]->in_flight.size() < best->in_flight.size()) {
                best = slots[i].get();
            }
        }
        return best;
    }

    ~PgPipelinePool() {
        for (auto& s : slots) {
            if (s->conn) PQfinish(s->conn);
        }
        slots.clear();
    }
};

inline thread_local std::unique_ptr<PgPipelinePool> tls_pipeline_pool;
inline thread_local std::array<DbItem, 64> tls_db_items;
inline thread_local std::string tls_db_scratch;

aegon::core::Task<void> run_slot_reader(aegon::core::EventLoop& loop, PipelineSlot* slot) {
    while (true) {
        (void)co_await loop.ring().poll(slot->fd, POLLIN);
        if (PQconsumeInput(slot->conn) == 0) {
            break;
        }

        while (!PQisBusy(slot->conn)) {
            PGresult* r = PQgetResult(slot->conn);
            if (!r) break;
            ExecStatusType st = PQresultStatus(r);
            if (st == PGRES_TUPLES_OK) {
                if (!slot->in_flight.empty()) {
                    auto [h, out_res] = slot->in_flight.front();
                    slot->in_flight.pop_front();
                    *out_res = r;
                    h.resume();
                } else {
                    PQclear(r);
                }
            } else if (st == PGRES_PIPELINE_SYNC) {
                PQclear(r);
            } else {
                if (!slot->in_flight.empty()) {
                    auto [h, out_res] = slot->in_flight.front();
                    slot->in_flight.pop_front();
                    *out_res = r;
                    h.resume();
                } else {
                    PQclear(r);
                }
            }
        }
    }
}

aegon::core::Task<void> handle_async_db(Context& ctx) {
    auto* loop = aegon::core::EventLoop::current();
    if (!tls_pipeline_pool) {
        const char* pg_conn = std::getenv("PG_CONNINFO");
        std::string conninfo = pg_conn ? pg_conn : "host=127.0.0.1 port=5432 user=bench password=bench dbname=benchmark";
        tls_pipeline_pool = std::make_unique<PgPipelinePool>(conninfo, 8);
        for (auto& s : tls_pipeline_pool->slots) {
            loop->spawn(run_slot_reader(*loop, s.get()));
        }
    }

    int min_val = 10;
    int max_val = 50;
    int limit_val = 50;

    std::string_view q = ctx.req().query();
    while (!q.empty()) {
        size_t amp = q.find('&');
        std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            std::string_view key = pair.substr(0, eq);
            std::string_view val = pair.substr(eq + 1);
            int v = 0;
            if (std::from_chars(val.data(), val.data() + val.size(), v).ec == std::errc()) {
                if (key == "min") min_val = v;
                else if (key == "max") max_val = v;
                else if (key == "limit") limit_val = std::clamp(v, 1, 50);
            }
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }

    PipelineSlot* slot = tls_pipeline_pool->pick_slot();
    if (!slot) {
        ctx.res().status(StatusCode::ServiceUnavailable).json(R"({"items":[],"count":0})");
        co_return;
    }

    char min_buf[16], max_buf[16], lim_buf[16];
    auto [p1, _1] = std::to_chars(min_buf, min_buf + sizeof(min_buf), min_val);
    *p1 = '\0';
    auto [p2, _2] = std::to_chars(max_buf, max_buf + sizeof(max_buf), max_val);
    *p2 = '\0';
    auto [p3, _3] = std::to_chars(lim_buf, lim_buf + sizeof(lim_buf), limit_val);
    *p3 = '\0';

    const char* paramValues[3] = { min_buf, max_buf, lim_buf };
    int sent = PQsendQueryPrepared(slot->conn, "stmt_async_db", 3, paramValues, nullptr, nullptr, 1);
    if (!sent) {
        ctx.res().status(StatusCode::InternalServerError).json(R"({"items":[],"count":0})");
        co_return;
    }
    PQpipelineSync(slot->conn);

    while (true) {
        int flush_res = PQflush(slot->conn);
        if (flush_res <= 0) break;
        (void)co_await loop->ring().poll(slot->fd, POLLOUT);
    }

    PGresult* res = co_await PipelineAwaiter{*slot};

    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        ctx.res().status(StatusCode::InternalServerError).json(R"({"items":[],"count":0})");
        co_return;
    }

    int n_rows = PQntuples(res);
    size_t count = std::min(static_cast<size_t>(n_rows), tls_db_items.size());

    for (size_t i = 0; i < count; ++i) {
        int r = static_cast<int>(i);

        uint32_t raw_id = 0;
        std::memcpy(&raw_id, PQgetvalue(res, r, 0), sizeof(uint32_t));
        int64_t id = static_cast<int32_t>(__builtin_bswap32(raw_id));

        const char* name_ptr = PQgetvalue(res, r, 1);
        int name_len = PQgetlength(res, r, 1);

        const char* cat_ptr = PQgetvalue(res, r, 2);
        int cat_len = PQgetlength(res, r, 2);

        uint32_t raw_price = 0;
        std::memcpy(&raw_price, PQgetvalue(res, r, 3), sizeof(uint32_t));
        int price = static_cast<int32_t>(__builtin_bswap32(raw_price));

        uint32_t raw_qty = 0;
        std::memcpy(&raw_qty, PQgetvalue(res, r, 4), sizeof(uint32_t));
        int qty = static_cast<int32_t>(__builtin_bswap32(raw_qty));

        bool active = (PQgetvalue(res, r, 5)[0] != 0);

        const char* tags_ptr = PQgetvalue(res, r, 6);
        int tags_len = PQgetlength(res, r, 6);
        if (tags_len > 0 && tags_ptr[0] == 0x01) {
            tags_ptr++;
            tags_len--;
        }

        uint32_t raw_score = 0;
        std::memcpy(&raw_score, PQgetvalue(res, r, 7), sizeof(uint32_t));
        int score = static_cast<int32_t>(__builtin_bswap32(raw_score));

        uint32_t raw_rc = 0;
        std::memcpy(&raw_rc, PQgetvalue(res, r, 8), sizeof(uint32_t));
        int64_t r_count = static_cast<int32_t>(__builtin_bswap32(raw_rc));

        tls_db_items[i] = DbItem{
            .id = id,
            .name = std::string_view(name_ptr, name_len),
            .category = std::string_view(cat_ptr, cat_len),
            .price = price,
            .quantity = qty,
            .active = active,
            .tags = glz::raw_json{std::string_view(tags_ptr, tags_len)},
            .rating = DbRating{.score = score, .count = r_count}
        };
    }

    DbResponse db_resp{
        .items = std::span<const DbItem>(tls_db_items.data(), count),
        .count = count
    };

    tls_db_scratch.clear();
    (void)glz::write_json(db_resp, tls_db_scratch);

    PQclear(res);

    ctx.res().status(StatusCode::Ok).json(tls_db_scratch);
}

// ── Main Entrypoint ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    size_t threads = 2;
    uint16_t port = 18090;

    if (argc > 1) {
        threads = static_cast<size_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    load_dataset();

    Server server;
    server.buffer_pool_entries(16384);
    server.ring_entries(8192);

    // Profile 1: Baseline endpoints
    server.router().get("/baseline11", handle_baseline_get);
    server.router().post("/baseline11", handle_baseline_post);
    server.router().get("/baseline2", handle_baseline_get);
    server.router().post("/baseline2", handle_baseline_post);

    // Profile 2: Async delay endpoint
    server.router().get("/delay/:ms", handle_delay);

    // Profile 3: JSON & Compression endpoint (SIMD libdeflate)
    server.router().get("/json/:count", {aegon::http::middleware::Compress({.min_size = 100})}, handle_json);

    // Profile 4: Async PostgreSQL database endpoint
    server.router().get("/async-db", handle_async_db);

    std::cout << "Aegon appstack benchmark listening on http://0.0.0.0:" << port << " with " << threads << " worker threads" << std::endl;
    server.listen(port);
    server.run(threads);
    return 0;
}
