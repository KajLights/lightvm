pico_sdk_init()
target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/platform.c")
target_compile_definitions(lightvm PRIVATE PICO_STACK_SIZE=16384)
target_link_libraries(lightvm PRIVATE pico_stdlib pico_stdio_usb hardware_pio hardware_flash hardware_sync)
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
