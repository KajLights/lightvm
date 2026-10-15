get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(SRCS "../main.c" "../runtime.c" "../../vm.c" "${root}/platforms/esp32/main/ws2812_encoder.c"
  INCLUDE_DIRS "../.." "${root}/platforms/esp32/main"
  REQUIRES esp_driver_rmt esp_driver_gpio esp_driver_usb_serial_jtag esp_timer esp_partition esp_hw_support)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wall -Wextra -Werror -ffp-contract=off)
