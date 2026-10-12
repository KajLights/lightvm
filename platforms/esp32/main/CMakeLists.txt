idf_component_register(SRCS "main.c" "platform.c" "ws2812_encoder.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_rmt esp_driver_usb_serial_jtag esp_timer esp_partition)
include("${LIGHTVM_ROOT}/cmake/lightvm_core.cmake")
lightvm_add_core()
target_link_libraries(${COMPONENT_LIB} PUBLIC lightvm_core)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
# Ordinary libraries must inherit the IDF compile options too (notably Xtensa
# long calls and section placement). No SDK dependency enters the shared source.
target_link_libraries(wasm3 PRIVATE idf::esp_common)
target_link_libraries(lightvm_core PRIVATE idf::esp_common)
