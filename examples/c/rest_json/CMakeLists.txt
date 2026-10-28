add_executable(c_example_rest_json main.c)
target_link_libraries(c_example_rest_json PRIVATE aegon_c)
set_target_properties(c_example_rest_json PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
if(MSVC)
    target_compile_options(c_example_rest_json PRIVATE /W4 /WX)
else()
    target_compile_options(c_example_rest_json PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

