# ==============================================================================
# Public C Header Hygiene Verification
# ==============================================================================

file(GLOB AEGON_C_PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/aegon/c/*.h")

set(HEADER_CHECK_TARGETS "")
foreach(hdr ${AEGON_C_PUBLIC_HEADERS})
    get_filename_component(hdr_name ${hdr} NAME)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_custom_target(c_header_check_${hdr_name}
            COMMAND ${CMAKE_C_COMPILER} -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only
                    -I${CMAKE_CURRENT_SOURCE_DIR}/include ${hdr}
            COMMENT "Verifying C11 syntax hygiene for aegon/c/${hdr_name}"
            VERBATIM
        )
        list(APPEND HEADER_CHECK_TARGETS c_header_check_${hdr_name})
    endif()
endforeach()

if(HEADER_CHECK_TARGETS)
    add_custom_target(c-abi-header-check DEPENDS ${HEADER_CHECK_TARGETS})
endif()

