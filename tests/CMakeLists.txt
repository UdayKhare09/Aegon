# ==============================================================================
# Aegon Test Suite & CTest Registrations
# ==============================================================================

# Ensure test assertions (assert) are never stripped out by -DNDEBUG in Release builds
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

function(aegon_add_test name)
    if(ARGN)
        set(all_targets_exist TRUE)
        foreach(dep ${ARGN})
            if(dep MATCHES "^aegon_" AND NOT TARGET ${dep})
                set(all_targets_exist FALSE)
            endif()
        endforeach()
        if(all_targets_exist)
            add_executable(test_${name} test_${name}.cpp)
            target_link_libraries(test_${name} PRIVATE ${ARGN})
            if("aegon_http" IN_LIST ARGN)
                target_precompile_headers(test_${name} REUSE_FROM aegon_http)
            elseif("aegon_core" IN_LIST ARGN)
                target_precompile_headers(test_${name} REUSE_FROM aegon_core)
            endif()
            if(MSVC)
                target_link_options(test_${name} PRIVATE /STACK:8388608)
            endif()
            add_test(NAME ${name} COMMAND test_${name})
        endif()
    else()
        if(TARGET aegon_http AND TARGET aegon_core)
            add_executable(test_${name} test_${name}.cpp)
            target_link_libraries(test_${name} PRIVATE aegon_http aegon_core)
            target_precompile_headers(test_${name} REUSE_FROM aegon_http)
            if(MSVC)
                target_link_options(test_${name} PRIVATE /STACK:8388608)
            endif()
            add_test(NAME ${name} COMMAND test_${name})
        endif()
    endif()
endfunction()

# Core & Utilities
add_executable(test_version test_version.cpp)
target_include_directories(test_version PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME version COMMAND test_version)

aegon_add_test(simd_string aegon_core)
aegon_add_test(net aegon_net aegon_core)
aegon_add_test(logger aegon_log)
if(TARGET glaze::glaze)
    aegon_add_test(foundational_types aegon_core glaze::glaze)
else()
    aegon_add_test(foundational_types aegon_core)
endif()
aegon_add_test(dto_validation)
if(WIN32)
    aegon_add_test(iocp_engine aegon_core ws2_32 mswsock ntdll)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    aegon_add_test(advanced_io)
    aegon_add_test(epoll_engine aegon_core aegon_http)
endif()
aegon_add_test(error_handling)

# HTTP & Web Protocols
aegon_add_test(radix_router)
aegon_add_test(simd_router)
aegon_add_test(middleware)
aegon_add_test(access_log)
aegon_add_test(cors)
aegon_add_test(security_headers)
aegon_add_test(cookie)
aegon_add_test(jwt)
aegon_add_test(auth_middleware)
aegon_add_test(rbac_guard)
aegon_add_test(jwks)
aegon_add_test(compress aegon_http aegon_core ${AEGON_DEFLATE_LIB})
aegon_add_test(http_client)
aegon_add_test(service_registry)
aegon_add_test(websocket)
aegon_add_test(forms)
aegon_add_test(config aegon_config)

# Data, Redis & ORM
aegon_add_test(redis_protocol aegon_redis aegon_core)
aegon_add_test(redis_advanced aegon_redis aegon_core ${AEGON_CRYPTO_LIB})
aegon_add_test(live_redis_topologies aegon_redis aegon_core)
aegon_add_test(memstore aegon_memory aegon_core)
aegon_add_test(sql_schema aegon_orm)
aegon_add_test(sql_query_builder aegon_orm)
aegon_add_test(sql_transaction aegon_orm aegon_http)
aegon_add_test(sql_relations aegon_orm)
aegon_add_test(sql_advanced_queries aegon_orm)
aegon_add_test(sql_drivers aegon_orm)
aegon_add_test(percore_clients aegon_orm aegon_http)
aegon_add_test(server_sql_config aegon_orm aegon_http)
aegon_add_test(orm_cache aegon_orm)
aegon_add_test(orm_advanced aegon_orm)
aegon_add_test(sql_tx_cache_parity aegon_orm)
aegon_add_test(live_postgres aegon_orm aegon_http)
aegon_add_test(live_postgres_orm_cache aegon_orm aegon_redis)
aegon_add_test(storage aegon_storage aegon_core aegon_memory)


# Standalone Test Servers & Harnesses (Non-CTest Executables)
add_executable(test_uuid test_uuid.cpp)
target_link_libraries(test_uuid PRIVATE aegon_core)

if(NOT WIN32)
    add_executable(test_async_core test_async_core.cpp)
    target_link_libraries(test_async_core PRIVATE aegon_core)
endif()

if(TARGET aegon_http)
    add_executable(test_http_server test_http_server.cpp)
    target_link_libraries(test_http_server PRIVATE aegon_http aegon_core)

    add_executable(test_http2_server test_http2_server.cpp)
    target_link_libraries(test_http2_server PRIVATE aegon_http aegon_core)

    add_executable(test_http3_server test_http3_server.cpp)
    target_link_libraries(test_http3_server PRIVATE aegon_http aegon_core)

    add_executable(test_http3_compliance test_http3_compliance.cpp)
    target_link_libraries(test_http3_compliance PRIVATE aegon_http aegon_core)

    add_executable(test_http_file_serving test_http_file_serving.cpp)
    target_link_libraries(test_http_file_serving PRIVATE aegon_http aegon_storage aegon_core)

    add_executable(test_tls_server test_tls_server.cpp)
    target_link_libraries(test_tls_server PRIVATE aegon_http aegon_core)

    add_executable(test_spec_server test_spec_server.cpp)
    target_link_libraries(test_spec_server PRIVATE aegon_http aegon_core)

    add_executable(autobahn_server autobahn/server.cpp)
    target_link_libraries(autobahn_server PRIVATE aegon_http aegon_core)

    add_executable(test_sse test_sse.cpp)
    target_link_libraries(test_sse PRIVATE aegon_http aegon_core)
    add_test(NAME sse COMMAND test_sse)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/proto/benchmark.pb.cc ${CMAKE_CURRENT_SOURCE_DIR}/proto/benchmark.pb.h
        COMMAND protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}/proto ${CMAKE_CURRENT_SOURCE_DIR}/proto/benchmark.proto
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/benchmark.proto
        COMMENT "Compiling benchmark.proto with protoc"
    )

    add_executable(test_grpc_unary test_grpc_unary.cpp proto/benchmark.pb.cc)
    target_include_directories(test_grpc_unary PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/proto)
    target_link_libraries(test_grpc_unary PRIVATE aegon_http aegon_core ${AEGON_PROTOBUF_LIB})
    add_test(NAME grpc_unary COMMAND test_grpc_unary)

    add_executable(test_grpc_streaming test_grpc_streaming.cpp proto/benchmark.pb.cc)
    target_include_directories(test_grpc_streaming PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/proto)
    target_link_libraries(test_grpc_streaming PRIVATE aegon_http aegon_core ${AEGON_PROTOBUF_LIB})
    add_test(NAME grpc_streaming COMMAND test_grpc_streaming)

    add_executable(test_grpc_client_streaming test_grpc_client_streaming.cpp proto/benchmark.pb.cc)
    target_include_directories(test_grpc_client_streaming PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/proto)
    target_link_libraries(test_grpc_client_streaming PRIVATE aegon_http aegon_core ${AEGON_PROTOBUF_LIB})
    add_test(NAME grpc_client_streaming COMMAND test_grpc_client_streaming)

    add_executable(test_grpc_bidi_streaming test_grpc_bidi_streaming.cpp proto/benchmark.pb.cc)
    target_include_directories(test_grpc_bidi_streaming PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/proto)
    target_link_libraries(test_grpc_bidi_streaming PRIVATE aegon_http aegon_core ${AEGON_PROTOBUF_LIB})
    add_test(NAME grpc_bidi_streaming COMMAND test_grpc_bidi_streaming)
endif()

# ==============================================================================
# Pure C ABI Tests
# ==============================================================================
if(AEGON_BUILD_C_ABI AND TARGET aegon_c)
    function(aegon_add_c_test name source_file)
        add_executable(c_${name} ${source_file})
        target_link_libraries(c_${name} PRIVATE aegon_c)
        set_target_properties(c_${name} PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        if(MSVC)
            target_compile_options(c_${name} PRIVATE /W4 /WX)
        else()
            target_compile_options(c_${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_definitions(c_${name} PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
        endif()
        add_test(NAME c_${name} COMMAND c_${name})
    endfunction()

    aegon_add_c_test(test_core c/test_core.c)
    aegon_add_c_test(test_log c/test_log.c)
    aegon_add_c_test(test_request c/test_request.c)
    aegon_add_c_test(test_response c/test_response.c)
    aegon_add_c_test(test_router c/test_router.c)
    aegon_add_c_test(test_group c/test_group.c)
    aegon_add_c_test(test_server c/test_server.c)
    aegon_add_c_test(test_middleware c/test_middleware.c)
    aegon_add_c_test(test_cors c/test_cors.c)
    aegon_add_c_test(test_compress c/test_compress.c)
    aegon_add_c_test(test_security c/test_security.c)
    aegon_add_c_test(test_static_files c/test_static_files.c)
    aegon_add_c_test(test_multipart c/test_multipart.c)
    aegon_add_c_test(test_async c/test_async.c)
    aegon_add_c_test(test_websocket c/test_websocket.c)
    aegon_add_c_test(test_sse c/test_sse.c)
    aegon_add_c_test(test_all_headers c/all_headers.c)

    add_executable(c_test_core_static c/test_core.c)
    target_link_libraries(c_test_core_static PRIVATE aegon_c_static)
    set_target_properties(c_test_core_static PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(c_test_core_static PRIVATE /W4 /WX)
    else()
        target_compile_options(c_test_core_static PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_definitions(c_test_core_static PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
    endif()
    add_test(NAME c_test_core_static COMMAND c_test_core_static)
endif()
