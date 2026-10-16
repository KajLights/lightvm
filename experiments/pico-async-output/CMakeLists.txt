# The optional probe project uses the standard production port.
set(experiment_dir "${CMAKE_CURRENT_LIST_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../../platforms/pico/CMakeLists.txt")

# A separate, short-lived firmware probes the production driver. It never opens
# the application store or initializes Wi-Fi; normal app A/B builds still do.
add_executable(output_probe "${experiment_dir}/probe.c")
target_compile_options(output_probe PRIVATE -Wall -Wextra -Werror)
target_include_directories(output_probe PRIVATE "${CMAKE_SOURCE_DIR}/platforms/pico")
target_link_libraries(output_probe PRIVATE lightvm_core pico_stdlib pico_stdio_usb
    pico_unique_id pico_rand hardware_pio hardware_dma hardware_flash hardware_sync)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/probe_generated")
pico_generate_pio_header(output_probe "${CMAKE_SOURCE_DIR}/platforms/pico/ws2812.pio"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/probe_generated")
pico_generate_pio_header(output_probe "${experiment_dir}/monitor.pio"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/probe_generated")
pico_set_double_implementation(output_probe compiler)
pico_enable_stdio_usb(output_probe 1)
pico_enable_stdio_uart(output_probe 0)
pico_add_extra_outputs(output_probe)
