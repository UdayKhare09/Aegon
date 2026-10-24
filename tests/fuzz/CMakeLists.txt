# ==============================================================================
# Aegon Security: LibFuzzer Targets
# ==============================================================================

# Target 1: HTTP/1.1 Request and Response Parser
add_executable(fuzz_http1_parser fuzz_http1_parser.cpp)
target_link_libraries(fuzz_http1_parser PRIVATE aegon_http aegon_core aegon_log)

# Target 2: WebSocket Frame Parser & SIMD Unmasking
add_executable(fuzz_websocket fuzz_websocket.cpp)
target_link_libraries(fuzz_websocket PRIVATE aegon_http aegon_core aegon_log)

# Target 3: Multipart/form-data MIME Parser
add_executable(fuzz_multipart fuzz_multipart.cpp)
target_link_libraries(fuzz_multipart PRIVATE aegon_http aegon_core aegon_log)

# Target 4: URL Parser & Percent-Encoding
add_executable(fuzz_url fuzz_url.cpp)
target_link_libraries(fuzz_url PRIVATE aegon_http aegon_core aegon_log)
