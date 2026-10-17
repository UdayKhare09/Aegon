#include "Version.h"
#include <cassert>
#include <iostream>

int main() {
    // 1. Verify macros
    static_assert(AEGON_VERSION_MAJOR == 0);
    static_assert(AEGON_VERSION_MINOR == 2);
    static_assert(AEGON_VERSION_PATCH == 1);
    static_assert(AEGON_VERSION_HEX == 0x000201);

    // 2. Conditional compilation macro check
#if AEGON_VERSION_HEX < AEGON_VERSION_CHECK(0, 2, 1)
    #error "AEGON_VERSION_HEX should be >= 0.2.1"
#endif

#if AEGON_VERSION_HEX >= AEGON_VERSION_CHECK(0, 3, 0)
    #error "AEGON_VERSION_HEX should be < 0.3.0"
#endif

    // 3. Runtime/constexpr VersionInfo struct check
    static_assert(aegon::version.major == 0);
    static_assert(aegon::version.minor == 2);
    static_assert(aegon::version.patch == 1);
    static_assert(aegon::version.prerelease == "");
    static_assert(aegon::version.string == "0.2.1");
    static_assert(aegon::version.is_at_least(0, 2, 1));
    static_assert(!aegon::version.is_at_least(0, 3, 0));

    std::cout << "Aegon version: " << aegon::version.string << " (" << AEGON_VERSION_STRING << ") - All version tests passed!\n";
    return 0;
}
