set(cr_backend "$ENV{CR_DEVICE_BACKEND}")
set(deps esp_driver_rmt esp_driver_gpio esp_driver_usb_serial_jtag esp_timer esp_hw_support pthread)
if(cr_backend STREQUAL "wamr")
  list(APPEND deps wamr)
endif()
get_filename_component(cr_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(
  SRCS "../core.c" "../backend_${cr_backend}.c"
       "${cr_root}/platforms/esp32/main/ws2812_encoder.c"
  INCLUDE_DIRS ".." "${cr_root}/platforms/esp32/main"
  REQUIRES ${deps})
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-unused-function)
