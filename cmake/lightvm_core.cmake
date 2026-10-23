# Reuse this build from a native SDK project without the host/Pico executable.
# The caller sets LIGHTVM_BOARD_CONFIG.
function(lightvm_add_core)
    get_filename_component(LIGHTVM_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    if(NOT EXISTS "${LIGHTVM_BOARD_CONFIG}")
        message(FATAL_ERROR "Set LIGHTVM_BOARD_CONFIG to the port's board/resource header")
    endif()
    include("${LIGHTVM_ROOT}/cmake/lightvm_release.cmake")
    include("${LIGHTVM_ROOT}/cmake/lightvm_storage.cmake")
    configure_file("${LIGHTVM_ROOT}/cmake/lightvm_release.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/lightvm_release.h" @ONLY)
    add_library(lightvm_config INTERFACE)
    include("${LIGHTVM_ROOT}/cmake/lightvm_resources.cmake")
    target_include_directories(lightvm_config INTERFACE "${LIGHTVM_ROOT}/src" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(lightvm_config INTERFACE
        LIGHTVM_BOARD_CONFIG="${LIGHTVM_BOARD_CONFIG}"
        LIGHTVM_WIFI=$<BOOL:${LIGHTVM_WIFI}> LIGHTVM_HTTP=$<BOOL:${LIGHTVM_HTTP}>
        LIGHTVM_WEBSOCKET=$<BOOL:${LIGHTVM_WEBSOCKET}> LIGHTVM_MQTT=$<BOOL:${LIGHTVM_MQTT}>)
    target_compile_definitions(lightvm_config INTERFACE LIGHTVM_BOOT_SEQUENCE=$<BOOL:${LIGHTVM_BOOT_SEQUENCE}>)

    target_compile_definitions(lightvm_config INTERFACE STORAGE_BYTES=${LIGHTVM_STORAGE_BYTES_VALUE} LIGHTVM_STORAGE_BACKEND=${LIGHTVM_STORAGE_BACKEND})
    if(LIGHTVM_STORAGE_CONFIGURATION)
        target_compile_definitions(lightvm_config INTERFACE LIGHTVM_STORAGE_CONFIGURATION=${LIGHTVM_STORAGE_CONFIGURATION})
    endif()

    add_library(device_config INTERFACE)
    target_compile_definitions(device_config INTERFACE
        DEVICE_WIFI=$<BOOL:${LIGHTVM_WIFI}> DEVICE_HTTP=$<BOOL:${LIGHTVM_HTTP}>
        DEVICE_WEBSOCKET=$<BOOL:${LIGHTVM_WEBSOCKET}>
        DEVICE_HTTP_CLIENTS=LIGHTVM_HTTP_CLIENTS
        DEVICE_HTTP_RESPONSE_BYTES=LIGHTVM_DEVICE_HTTP_RESPONSE_BYTES
        DEVICE_STORAGE_BACKEND=${LIGHTVM_STORAGE_BACKEND})
    # Match the application's bounded reply capacity, including module downloads.
    target_link_libraries(device_config INTERFACE lightvm_config)
    target_compile_options(device_config INTERFACE -include "${LIGHTVM_ROOT}/src/device_config.h")
    if(LIGHTVM_STORAGE_CONFIGURATION)
        target_compile_definitions(device_config INTERFACE DEVICE_STORAGE_CONFIGURATION=${LIGHTVM_STORAGE_CONFIGURATION})
    endif()
    set(DEVICE_BUILD_HTTP ${LIGHTVM_HTTP})
    set(DEVICE_BUILD_WEBSOCKET ${LIGHTVM_WEBSOCKET})
    add_subdirectory("${LIGHTVM_ROOT}/shared/device-core" "${CMAKE_CURRENT_BINARY_DIR}/device-core")
    add_library(lightvm_core STATIC "${LIGHTVM_ROOT}/src/store_policy.c" "${LIGHTVM_ROOT}/src/device_host.c"
        "${LIGHTVM_ROOT}/src/release.c" "${LIGHTVM_ROOT}/src/runtime.c" "${LIGHTVM_ROOT}/src/render_executor.c" "${LIGHTVM_ROOT}/src/app.c"
        "${LIGHTVM_ROOT}/src/vm_allocator.c" "${LIGHTVM_ROOT}/src/pixels.c" "${LIGHTVM_ROOT}/src/numeric.c" "${LIGHTVM_ROOT}/src/native_math.c"
        "${LIGHTVM_ROOT}/src/session.c" "${LIGHTVM_ROOT}/src/vm.c" "${LIGHTVM_ROOT}/src/command.c"
        "${LIGHTVM_ROOT}/src/preparation.c" "${LIGHTVM_ROOT}/src/control.c" "${LIGHTVM_ROOT}/src/inputs.c" "${LIGHTVM_ROOT}/src/portable_manifest.c" "${LIGHTVM_ROOT}/src/bytecode_core.c"
        "${LIGHTVM_ROOT}/src/portable.c" "${LIGHTVM_ROOT}/src/portable_draw.c" "${LIGHTVM_ROOT}/src/portable_map.c" "${LIGHTVM_ROOT}/src/animation.c" "${LIGHTVM_ROOT}/src/draw.c" "${LIGHTVM_ROOT}/src/composition.c" "${LIGHTVM_ROOT}/src/transition.c"
        "${LIGHTVM_ROOT}/src/outputs.c" "${LIGHTVM_ROOT}/src/digital_inputs.c" "${LIGHTVM_ROOT}/src/apa102.c")
    if(LIGHTVM_STORAGE_BACKEND_SOURCES)
        target_sources(lightvm_core PRIVATE ${LIGHTVM_STORAGE_BACKEND_SOURCES})
    endif()
    if(LIGHTVM_MQTT)
        target_sources(lightvm_core PRIVATE "${LIGHTVM_ROOT}/src/mqtt.c" "${LIGHTVM_ROOT}/src/home_assistant.c" "${LIGHTVM_ROOT}/src/ha_keys.c")
    endif()
    if(LIGHTVM_BOOT_SEQUENCE)
        target_sources(lightvm_core PRIVATE "${LIGHTVM_ROOT}/src/boot_sequence.c")
    endif()
    target_compile_features(lightvm_core PUBLIC c_std_11)
    target_compile_options(lightvm_core PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
    target_link_libraries(lightvm_core PUBLIC lightvm_config device_core m)
endfunction()
