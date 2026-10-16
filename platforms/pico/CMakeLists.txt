if(LIGHTVM_HTTP AND NOT LIGHTVM_WIFI)
    message(FATAL_ERROR "Embedded HTTP requires LIGHTVM_WIFI=ON")
endif()
if(LIGHTVM_WIFI AND NOT PICO_BOARD STREQUAL "pico_w")
    message(FATAL_ERROR "Wi-Fi requires PICO_BOARD=pico_w")
endif()
pico_sdk_init()
target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/platform.c" "${CMAKE_CURRENT_LIST_DIR}/pixels.c")
target_compile_definitions(lightvm PRIVATE PICO_STACK_SIZE=16384)
target_link_libraries(lightvm PRIVATE pico_stdlib pico_stdio_usb pico_unique_id pico_rand hardware_pio hardware_dma hardware_flash hardware_sync)
pico_generate_pio_header(lightvm "${CMAKE_CURRENT_LIST_DIR}/ws2812.pio")
# SDK 1.5.1's ROM-v1 double shim is licensed for Pico devices only.
# Use the compiler runtime so firmware avoids that restricted component.
pico_set_double_implementation(lightvm compiler)
pico_enable_stdio_usb(lightvm 1)
pico_enable_stdio_uart(lightvm 0)
pico_add_extra_outputs(lightvm)

# Reserve the final 512 KiB; fail the link rather than permit firmware overlap.
configure_file("${CMAKE_CURRENT_LIST_DIR}/storage.ld" "${CMAKE_CURRENT_BINARY_DIR}/storage.ld" COPYONLY)
target_link_options(lightvm PRIVATE "LINKER:-T,${CMAKE_CURRENT_BINARY_DIR}/storage.ld")
if(LIGHTVM_BOOT_SEQUENCE)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/storage_boot_sequence.ld" "${CMAKE_CURRENT_BINARY_DIR}/storage_boot_sequence.ld" COPYONLY)
    target_link_options(lightvm PRIVATE "LINKER:-T,${CMAKE_CURRENT_BINARY_DIR}/storage_boot_sequence.ld")
endif()

if(LIGHTVM_WIFI)
    target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/network_port.c"
        "${CMAKE_CURRENT_LIST_DIR}/dhcp_port.c" "${CMAKE_SOURCE_DIR}/src/dhcp.c")
    target_include_directories(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions(lightvm PRIVATE CYW43_SPI_PIO_PREFERRED_PIO=1)
    target_link_libraries(lightvm PRIVATE pico_cyw43_arch_lwip_poll pico_unique_id pico_rand)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(toolchain_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(LIGHTVM_ARM_SIZE arm-none-eabi-size HINTS "${toolchain_bin}" REQUIRED)
    add_custom_command(TARGET lightvm POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check_pico_ram.py" "${LIGHTVM_ARM_SIZE}" "$<TARGET_FILE:lightvm>")
endif()
