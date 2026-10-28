add_executable(c_example_static_site main.c)
target_link_libraries(c_example_static_site PRIVATE aegon_c)
set_target_properties(c_example_static_site PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
if(MSVC)
    target_compile_options(c_example_static_site PRIVATE /W4 /WX)
else()
    target_compile_options(c_example_static_site PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

