#include "log/Logger.h"
#include "log/MemorySink.h"
#include "log/FileSink.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <sstream>

void test_level_parsing() {
    using namespace aegon::log;
    assert(to_string(Level::Trace) == "TRACE");
    assert(to_string(Level::Debug) == "DEBUG");
    assert(to_string(Level::Info)  == "INFO");
    assert(to_string(Level::Warn)  == "WARN");
    assert(to_string(Level::Error) == "ERROR");
    assert(to_string(Level::Fatal) == "FATAL");
    assert(to_string(Level::Off)   == "OFF");

    assert(from_string("debug") == Level::Debug);
    assert(from_string("DEBUG") == Level::Debug);
    assert(from_string("WARN") == Level::Warn);
    assert(from_string("warning") == Level::Warn);
    assert(from_string("error") == Level::Error);
    assert(from_string("non_existent", Level::Trace) == Level::Trace);
    std::cout << "test_level_parsing passed\n";
}

void test_memory_sink_filtering() {
    using namespace aegon::log;
    auto mem_sink = std::make_shared<MemorySink>();

    init({.level = Level::Warn, .async = false});
    set_sinks({mem_sink});

    // Trace, Debug, Info should be ignored because level is Warn
    trace("This is trace");
    debug("This is debug: {}", 123);
    info("This is info");

    assert(mem_sink->size() == 0);

    // Warn, Error, Fatal should be captured
    warn("Warning message: {}", "low memory");
    error("Error code: {}", 500);
    fatal("Fatal crash imminent");

    assert(mem_sink->size() == 3);
    assert(mem_sink->contains("Warning message: low memory"));
    assert(mem_sink->contains("Error code: 500"));
    assert(mem_sink->contains("Fatal crash imminent"));

    auto recs = mem_sink->records();
    assert(recs[0].level == Level::Warn);
    assert(recs[1].level == Level::Error);
    assert(recs[2].level == Level::Fatal);

    // Verify source location
    assert(recs[0].file_basename() == "test_logger.cpp");
    assert(recs[0].location.line() > 0);

    // Dynamically change level to Debug
    set_level(Level::Debug);
    debug("Now debug works: {}", 42);
    assert(mem_sink->size() == 4);
    assert(mem_sink->contains("Now debug works: 42"));

    std::cout << "test_memory_sink_filtering passed\n";
}

void test_plain_strings_with_braces() {
    using namespace aegon::log;
    auto mem_sink = std::make_shared<MemorySink>();
    set_sinks({mem_sink});
    set_level(Level::Debug);

    // Plain JSON string with braces without format args should not throw or fail format parsing
    info("{\"user\": \"uday\", \"id\": 10}");
    assert(mem_sink->contains("{\"user\": \"uday\", \"id\": 10}"));

    std::cout << "test_plain_strings_with_braces passed\n";
}

void test_macros() {
    using namespace aegon::log;
    auto mem_sink = std::make_shared<MemorySink>();
    set_sinks({mem_sink});
    set_level(Level::Trace);

    AEGON_LOG_TRACE("Trace macro {}", 1);
    AEGON_LOG_DEBUG("Debug macro {}", 2);
    AEGON_LOG_INFO("Info macro {}", 3);
    AEGON_LOG_WARN("Warn macro {}", 4);
    AEGON_LOG_ERROR("Error macro {}", 5);
    AEGON_LOG_FATAL("Fatal macro {}", 6);

    assert(mem_sink->size() == 6);
    assert(mem_sink->contains("Trace macro 1"));
    assert(mem_sink->contains("Fatal macro 6"));

    std::cout << "test_macros passed\n";
}

void test_file_sink() {
    using namespace aegon::log;
    std::filesystem::path test_file = "build/test_aegon.log";
    std::filesystem::remove(test_file);

    {
        auto file_sink = std::make_shared<FileSink>(test_file, false);
        assert(file_sink->is_open());
        set_sinks({file_sink});
        set_level(Level::Info);

        info("Logged to file: value={}", 9999);
        warn("Warning in file");
        flush();
    }

    // Read back file
    std::ifstream in(test_file);
    assert(in.is_open());
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    assert(content.find("[INFO ]") != std::string::npos || content.find("[INFO]") != std::string::npos);
    assert(content.find("Logged to file: value=9999") != std::string::npos);
    assert(content.find("Warning in file") != std::string::npos);

    std::filesystem::remove(test_file);
    std::cout << "test_file_sink passed\n";
}

void test_async_multithreaded_logging() {
    using namespace aegon::log;
    auto mem_sink = std::make_shared<MemorySink>();

    init({
        .level = Level::Info,
        .async = true,
        .flush_interval_ms = 5
    });
    set_sinks({mem_sink});

    constexpr int num_threads = 4;
    constexpr int logs_per_thread = 250;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < logs_per_thread; ++i) {
                info("Thread {} message {}", t, i);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Wait and flush
    flush();

    assert(mem_sink->size() == num_threads * logs_per_thread);

    shutdown();
    std::cout << "test_async_multithreaded_logging passed ("
              << (num_threads * logs_per_thread) << " logs)\n";
}

int main() {
    std::cout << "Starting aegon_log tests...\n";
    test_level_parsing();
    test_memory_sink_filtering();
    test_plain_strings_with_braces();
    test_macros();
    test_file_sink();
    test_async_multithreaded_logging();
    std::cout << "All aegon_log tests PASSED!\n";
    return 0;
}
