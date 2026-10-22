# Test-only image: the production numeric module and target math configuration.
# No application store, network services or LED outputs are initialized.
add_executable(numeric_probe "${CMAKE_SOURCE_DIR}/tests/native_math_driver.c")
target_compile_options(numeric_probe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(numeric_probe PRIVATE lightvm_core pico_stdlib pico_stdio_usb)
pico_set_double_implementation(numeric_probe compiler)
pico_enable_stdio_usb(numeric_probe 1)
pico_enable_stdio_uart(numeric_probe 0)
pico_add_extra_outputs(numeric_probe)
