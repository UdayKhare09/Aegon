# ==============================================================================
# Aegon Security: Compiler & Linker Binary Hardening
# ==============================================================================

option(AEGON_HARDENING "Enable compiler and linker binary security hardening flags" ON)

if(AEGON_HARDENING)
    message(STATUS "[Security Hardening] Enabling production binary hardening")

    add_compile_options(
        -fstack-protector-strong  # Stack canaries on functions with buffers
        -fstack-clash-protection  # Prevent jumping over stack guard pages
        -Wformat=2                # Strict printf/scanf format checks
        -Wformat-security         # Disallow potential format string vulnerabilities
        -Wnull-dereference        # Warn on dereferencing null pointers
        -D_FORTIFY_SOURCE=3       # Runtime buffer bounds check
    )

    add_link_options(
        "-Wl,-z,relro,-z,now"   # Full RELRO: make GOT read-only after dynamic binding
        "-Wl,-z,noexecstack"    # Mark stack as non-executable (NX/DEP)
    )
endif()
