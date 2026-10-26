if((LIGHTVM_HTTP OR LIGHTVM_MQTT) AND NOT LIGHTVM_WIFI)
    message(FATAL_ERROR "Embedded HTTP/MQTT requires LIGHTVM_WIFI=ON")
endif()
if(LIGHTVM_AUDIO_MULTICAST AND NOT LIGHTVM_WIFI)
    message(FATAL_ERROR "Audio multicast requires LIGHTVM_WIFI=ON")
endif()
if(LIGHTVM_WIFI AND NOT PICO_BOARD MATCHES "^(pico_w|pico2_w)$")
    message(FATAL_ERROR "Wi-Fi requires PICO_BOARD=pico_w or pico2_w")
endif()
pico_sdk_init()
target_compile_options(lightvm_core PRIVATE -fstack-usage -fcallgraph-info=su)
target_compile_options(device_core PRIVATE -fstack-usage -fcallgraph-info=su)
target_compile_options(lightvm PRIVATE -fstack-usage -fcallgraph-info=su)
option(LIGHTVM_OUTPUT_PROBE "Build the Pico hardware output verification image" OFF)
option(LIGHTVM_NUMERIC_PROBE "Build the isolated numeric conformance image" OFF)
target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/platform.c" "${CMAKE_CURRENT_LIST_DIR}/board_status.c" "${CMAKE_CURRENT_LIST_DIR}/pixels.c" "${CMAKE_CURRENT_LIST_DIR}/peripherals.c" "${CMAKE_CURRENT_LIST_DIR}/storage_layout.S"
    "${CMAKE_CURRENT_LIST_DIR}/render_executor.c")
set(LIGHTVM_WORKER_STACK_BYTES "8192" CACHE STRING "Core 1 render stack reservation")
if(NOT LIGHTVM_WORKER_STACK_BYTES MATCHES "^[0-9]+$" OR
   LIGHTVM_WORKER_STACK_BYTES LESS 4096 OR LIGHTVM_WORKER_STACK_BYTES GREATER 32768)
    message(FATAL_ERROR "LIGHTVM_WORKER_STACK_BYTES must be 4096..32768")
endif()
math(EXPR worker_stack_remainder "${LIGHTVM_WORKER_STACK_BYTES} % 32")
if(worker_stack_remainder)
    message(FATAL_ERROR "LIGHTVM_WORKER_STACK_BYTES must be a multiple of 32")
endif()
target_compile_definitions(lightvm PRIVATE PICO_PANIC_FUNCTION=lightvm_panic PICO_STACK_SIZE=16384 PICO_CORE1_STACK_SIZE=${LIGHTVM_WORKER_STACK_BYTES})
# Queue complete ordinary control requests while the foreground renders. The
# SDK's 64-byte CDC FIFO otherwise splits JSON commands across render turns.
# A full operation-progress reply fits in TX without holding the foreground
# while the host drains individual USB packets.
# Keep endpoint transfers at one USB packet: a command ending on an exact
# 64-byte boundary must not wait for another packet or a host ZLP.
target_compile_definitions(lightvm PRIVATE CFG_TUD_CDC_RX_BUFSIZE=256 CFG_TUD_CDC_TX_BUFSIZE=1024 CFG_TUD_CDC_EP_BUFSIZE=64)
# Both stacks are excluded from the contiguous heap and checked in the RAM gate.
pico_set_linker_script_var(lightvm RAM_LENGTH "RAM_LENGTH_DEFAULT+SCRATCH_X_LENGTH_DEFAULT+SCRATCH_Y_LENGTH_DEFAULT-16384-${LIGHTVM_WORKER_STACK_BYTES}")
pico_set_linker_script_var(lightvm SCRATCH_X_ORIGIN "RAM_ORIGIN_DEFAULT+RAM_LENGTH_DEFAULT+SCRATCH_X_LENGTH_DEFAULT+SCRATCH_Y_LENGTH_DEFAULT-16384-${LIGHTVM_WORKER_STACK_BYTES}")
pico_set_linker_script_var(lightvm SCRATCH_X_LENGTH "${LIGHTVM_WORKER_STACK_BYTES}")
pico_set_linker_script_var(lightvm SCRATCH_Y_ORIGIN "RAM_ORIGIN_DEFAULT+RAM_LENGTH_DEFAULT+SCRATCH_X_LENGTH_DEFAULT+SCRATCH_Y_LENGTH_DEFAULT-16384")
pico_set_linker_script_var(lightvm SCRATCH_Y_LENGTH "16384")
# Compiler math code/constant tables run from XIP flash. Keep SDK functions
# explicitly marked .time_critical in RAM, including flash program/erase code.
pico_add_linker_script_override_path(lightvm "${CMAKE_CURRENT_LIST_DIR}/linker"
    FILES default_text_excludes.incl default_rodata_excludes.incl)
target_link_libraries(lightvm PRIVATE pico_stdlib pico_stdio_usb pico_unique_id pico_rand pico_multicore pico_flash hardware_pio hardware_dma hardware_flash hardware_sync hardware_i2c hardware_clocks hardware_adc)
pico_generate_pio_header(lightvm "${CMAKE_CURRENT_LIST_DIR}/ws2812.pio")
pico_generate_pio_header(lightvm "${CMAKE_CURRENT_LIST_DIR}/apa102.pio")
# Keep using the compiler runtime rather than the SDK's ROM double shims.
pico_set_double_implementation(lightvm compiler)
# RP2350's DCP accelerates double arithmetic used in clocks and transformed
# sprites. Keep compiler conversions and libm, preserving their edge semantics.
option(LIGHTVM_PICO2_DCP "Use RP2350 Arm double arithmetic coprocessor" ON)
if(PICO_RP2350 AND NOT PICO_RISCV AND LIGHTVM_PICO2_DCP)
    target_compile_definitions(lightvm_config INTERFACE LIGHTVM_PICO2_DCP_ENABLED=1)
    target_sources(lightvm PRIVATE "${PICO_SDK_PATH}/src/rp2_common/pico_double/double_aeabi_dcp.S")
    foreach(operation dadd dsub drsub dmul ddiv)
        # pico_wrap_function supplies INTERFACE options for SDK libraries; an
        # executable needs its own PRIVATE linker options.
        target_link_options(lightvm PRIVATE "LINKER:--wrap=__aeabi_${operation}")
    endforeach()
endif()
if(LIGHTVM_WIFI)
    target_link_options(lightvm PRIVATE "LINKER:--wrap=cyw43_send_ethernet")
endif()
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
add_custom_command(TARGET lightvm POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/audit_pico_stack.py"
        "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/stack-paths.json" --elf "$<TARGET_FILE:lightvm>" --objdump "${CMAKE_OBJDUMP}")
add_custom_target(pico_stack_audit
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/audit_pico_stack.py"
        "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/stack-paths.json" --elf "$<TARGET_FILE:lightvm>" --objdump "${CMAKE_OBJDUMP}"
    DEPENDS lightvm)

# Reserve the configured region relative to the SDK board flash geometry.
configure_file("${CMAKE_CURRENT_LIST_DIR}/storage.ld" "${CMAKE_CURRENT_BINARY_DIR}/storage.ld" COPYONLY)
target_link_options(lightvm PRIVATE "LINKER:-T,${CMAKE_CURRENT_BINARY_DIR}/storage.ld")
if(LIGHTVM_BOOT_SEQUENCE)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/storage_boot_sequence.ld" "${CMAKE_CURRENT_BINARY_DIR}/storage_boot_sequence.ld" COPYONLY)
    target_link_options(lightvm PRIVATE "LINKER:-T,${CMAKE_CURRENT_BINARY_DIR}/storage_boot_sequence.ld")
endif()

if(LIGHTVM_MQTT)
    target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/mqtt_port.c")
    set_property(SOURCE "${CMAKE_SOURCE_DIR}/src/home_assistant.c" APPEND PROPERTY COMPILE_OPTIONS "-fstack-usage")
    add_custom_command(TARGET lightvm POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check_pico_ha_stack.py"
            "${CMAKE_BINARY_DIR}/CMakeFiles/lightvm_core.dir/src/home_assistant.c.su"
            "${CMAKE_CURRENT_BINARY_DIR}/ha-stack-report.json")
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
if(LIGHTVM_NUMERIC_PROBE)
    include("${CMAKE_SOURCE_DIR}/tests/numeric_pico/CMakeLists.txt")
endif()

pico_set_program_version(lightvm "${LIGHTVM_VERSION}")
