#include "Version.h"
#include <cassert>
#include <iostream>

int main() {
    // 1. Verify macros
    static_assert(AEGON_VERSION_MAJOR == 0);
    static_assert(AEGON_VERSION_MINOR == 3);
    static_assert(AEGON_VERSION_PATCH == 2);
    static_assert(AEGON_VERSION_HEX == 0x000302);

    // 2. Conditional compilation macro check
#if AEGON_VERSION_HEX < AEGON_VERSION_CHECK(0, 3, 2)
    #error "AEGON_VERSION_HEX should be >= 0.3.2"
#endif

#if AEGON_VERSION_HEX >= AEGON_VERSION_CHECK(0, 4, 0)
    #error "AEGON_VERSION_HEX should be < 0.4.0"
#endif

    // 3. Runtime/constexpr VersionInfo struct check
    static_assert(aegon::version.major == 0);
    static_assert(aegon::version.minor == 3);
    static_assert(aegon::version.patch == 2);
    static_assert(aegon::version.prerelease == "");
    static_assert(aegon::version.string == "0.3.2");
    static_assert(aegon::version.is_at_least(0, 3, 2));
    static_assert(!aegon::version.is_at_least(0, 4, 0));

    std::cout << "Aegon version: " << aegon::version.string << " (" << AEGON_VERSION_STRING << ") - All version tests passed!\n";
    return 0;
}
