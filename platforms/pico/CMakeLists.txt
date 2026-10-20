if((LIGHTVM_HTTP OR LIGHTVM_MQTT) AND NOT LIGHTVM_WIFI)
    message(FATAL_ERROR "Embedded HTTP/MQTT requires LIGHTVM_WIFI=ON")
endif()
if(LIGHTVM_WIFI AND NOT PICO_BOARD MATCHES "^(pico_w|pico2_w)$")
    message(FATAL_ERROR "Wi-Fi requires PICO_BOARD=pico_w or pico2_w")
endif()
pico_sdk_init()
option(LIGHTVM_OUTPUT_PROBE "Build the Pico hardware output verification image" OFF)
target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/platform.c" "${CMAKE_CURRENT_LIST_DIR}/pixels.c" "${CMAKE_CURRENT_LIST_DIR}/peripherals.c" "${CMAKE_CURRENT_LIST_DIR}/storage_layout.S")
target_compile_definitions(lightvm PRIVATE PICO_STACK_SIZE=16384)
# This port is single-core. Give the heap all SRAM below the 16 KiB core-0
# stack, and no unused core-1 scratch region. SDK 2.3 enforces stack region size.
pico_set_linker_script_var(lightvm RAM_LENGTH "RAM_LENGTH_DEFAULT+SCRATCH_X_LENGTH_DEFAULT+SCRATCH_Y_LENGTH_DEFAULT-16384")
pico_set_linker_script_var(lightvm SCRATCH_X_ORIGIN "RAM_ORIGIN_DEFAULT+RAM_LENGTH_DEFAULT+SCRATCH_X_LENGTH_DEFAULT+SCRATCH_Y_LENGTH_DEFAULT-16384")
pico_set_linker_script_var(lightvm SCRATCH_X_LENGTH "0")
pico_set_linker_script_var(lightvm SCRATCH_Y_ORIGIN "RAM_ORIGIN_DEFAULT+RAM_LENGTH_DEFAULT+SCRATCH_X_LENGTH_DEFAULT+SCRATCH_Y_LENGTH_DEFAULT-16384")
pico_set_linker_script_var(lightvm SCRATCH_Y_LENGTH "16384")
# Compiler math code/constant tables run from XIP flash. Keep SDK functions
# explicitly marked .time_critical in RAM, including flash program/erase code.
pico_add_linker_script_override_path(lightvm "${CMAKE_CURRENT_LIST_DIR}/linker"
    FILES default_text_excludes.incl default_rodata_excludes.incl)
target_link_libraries(lightvm PRIVATE pico_stdlib pico_stdio_usb pico_unique_id pico_rand hardware_pio hardware_dma hardware_flash hardware_sync hardware_i2c hardware_clocks)
pico_generate_pio_header(lightvm "${CMAKE_CURRENT_LIST_DIR}/ws2812.pio")
pico_generate_pio_header(lightvm "${CMAKE_CURRENT_LIST_DIR}/apa102.pio")
# Keep using the compiler runtime rather than the SDK's ROM double shims.
pico_set_double_implementation(lightvm compiler)
pico_enable_stdio_usb(lightvm 1)
pico_enable_stdio_uart(lightvm 0)
pico_add_extra_outputs(lightvm)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET lightvm POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/write_storage_layout.py"
        "${CMAKE_NM}" "$<TARGET_FILE:lightvm>" "${CMAKE_CURRENT_BINARY_DIR}/storage-layout.json" "${LIGHTVM_STORAGE_BYTES_VALUE}" "${PICO_BOARD}")
add_custom_command(TARGET lightvm POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check_pico_ram.py"
        "${CMAKE_NM}" "$<TARGET_FILE:lightvm>")

# Reserve the configured region relative to the SDK board flash geometry.
configure_file("${CMAKE_CURRENT_LIST_DIR}/storage.ld" "${CMAKE_CURRENT_BINARY_DIR}/storage.ld" COPYONLY)
target_link_options(lightvm PRIVATE "LINKER:-T,${CMAKE_CURRENT_BINARY_DIR}/storage.ld")
if(LIGHTVM_BOOT_SEQUENCE)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/storage_boot_sequence.ld" "${CMAKE_CURRENT_BINARY_DIR}/storage_boot_sequence.ld" COPYONLY)
    target_link_options(lightvm PRIVATE "LINKER:-T,${CMAKE_CURRENT_BINARY_DIR}/storage_boot_sequence.ld")
endif()

if(LIGHTVM_MQTT)
    target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/mqtt_port.c")
endif()
if(LIGHTVM_WIFI)
    target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/network_port.c"
        "${CMAKE_CURRENT_LIST_DIR}/dhcp_port.c" "${CMAKE_SOURCE_DIR}/src/dhcp.c")
    target_include_directories(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(lightvm PRIVATE pico_cyw43_arch_lwip_poll pico_unique_id pico_rand)
endif()

if(LIGHTVM_OUTPUT_PROBE)
    include("${CMAKE_SOURCE_DIR}/tests/pico_output/CMakeLists.txt")
endif()

pico_set_program_version(lightvm "${LIGHTVM_VERSION}")
