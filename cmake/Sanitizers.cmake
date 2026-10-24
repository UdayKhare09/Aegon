# ==============================================================================
# Aegon Sanitizers Integration (ASan, UBSan, TSan, MSan)
# ==============================================================================

set(AEGON_SANITIZE "" CACHE STRING
    "Enable runtime sanitizers. Options: ON (ASan+UBSan), address, undefined, thread, memory, leak"
)

if(AEGON_SANITIZE)
    string(TOLOWER "${AEGON_SANITIZE}" _sanitize_lower)

    # Normalize ON / TRUE / 1 to address,undefined
    if(_sanitize_lower STREQUAL "on" OR _sanitize_lower STREQUAL "true" OR _sanitize_lower STREQUAL "1")
        set(_sanitize_lower "address,undefined")
    endif()

    if(MSVC)
        if(_sanitize_lower MATCHES "address")
            message(STATUS "[Sanitizers] MSVC AddressSanitizer enabled (/fsanitize=address)")
            add_compile_options(/fsanitize=address)
        else()
            message(WARNING "[Sanitizers] Only AddressSanitizer (/fsanitize=address) is supported on MSVC.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Validate mutual exclusivity
        if(_sanitize_lower MATCHES "address" AND _sanitize_lower MATCHES "thread")
            message(FATAL_ERROR "[Sanitizers] AddressSanitizer (address) and ThreadSanitizer (thread) are mutually exclusive.")
        endif()
        if(_sanitize_lower MATCHES "memory" AND (_sanitize_lower MATCHES "address" OR _sanitize_lower MATCHES "thread"))
            message(FATAL_ERROR "[Sanitizers] MemorySanitizer (memory) cannot be combined with address or thread sanitizers.")
        endif()

        # Core sanitizer compiler flags
        add_compile_options(
            "-fsanitize=${_sanitize_lower}"
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
            -g
        )

        # Core sanitizer linker flags
        add_link_options(
            "-fsanitize=${_sanitize_lower}"
        )

        message(STATUS "[Sanitizers] Runtime sanitizers active: ${_sanitize_lower}")
    else()
        message(WARNING "[Sanitizers] Compiler ${CMAKE_CXX_COMPILER_ID} not recognized for sanitizers.")
    endif()
endif()