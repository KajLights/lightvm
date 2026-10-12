cmake_minimum_required(VERSION 3.18)
option(LIGHTVM_PICO "Build USB CDC firmware for an RP2040 Pico" OFF)
if(LIGHTVM_PICO)
    if(NOT PICO_SDK_PATH)
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    endif()
    if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
        message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK checkout with its TinyUSB submodule")
    endif()
    include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
endif()
project(lightvm C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(LIGHTVM_PICO)
    pico_sdk_init()
endif()

set(WASM3_SOURCE_DIR "" CACHE PATH "Optional existing wasm3 v0.5.0 checkout")
if(NOT WASM3_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(wasm3
        URL https://codeload.github.com/wasm3/wasm3/tar.gz/refs/tags/v0.5.0
        URL_HASH SHA256=b778dd72ee2251f4fe9e2666ee3fe1c26f06f517c3ffce572416db067546536c)
    FetchContent_GetProperties(wasm3)
    if(NOT wasm3_POPULATED)
        FetchContent_Populate(wasm3)
    endif()
    set(WASM3_SOURCE_DIR "${wasm3_SOURCE_DIR}")
endif()

# Keep the dependency untouched. Add a cooperative timeout check to each loop
# iteration; v0.5.0 otherwise only calls its weak m3_Yield hook on function calls.
set(M3_DIR "${CMAKE_CURRENT_BINARY_DIR}/wasm3-source")
file(MAKE_DIRECTORY "${M3_DIR}")
file(COPY "${WASM3_SOURCE_DIR}/source/" DESTINATION "${M3_DIR}")
file(READ "${M3_DIR}/m3_exec.h" M3_EXEC)
set(LOOP_ORIGINAL "        r = nextOpImpl ();")
string(FIND "${M3_EXEC}" "${LOOP_ORIGINAL}" LOOP_OFFSET)
if(LOOP_OFFSET EQUAL -1)
    message(FATAL_ERROR "Expected wasm3 v0.5.0 loop implementation was not found")
endif()
string(REPLACE "${LOOP_ORIGINAL}"
    "        r = m3_Yield ();\n        if (r) return r;\n${LOOP_ORIGINAL}"
    M3_EXEC "${M3_EXEC}")
file(WRITE "${M3_DIR}/m3_exec.h" "${M3_EXEC}")

set(M3_FILES m3_bind.c m3_code.c m3_compile.c m3_core.c m3_emit.c
    m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c)
list(TRANSFORM M3_FILES PREPEND "${M3_DIR}/")
add_library(wasm3 STATIC ${M3_FILES})
target_include_directories(wasm3 PUBLIC "${M3_DIR}")
target_compile_definitions(wasm3 PRIVATE
    d_m3MaxLinearMemoryPages=1 d_m3MaxFunctionStackHeight=128
    d_m3CodePageAlignSize=1024 d_m3LogOutput=0)
# All interpreter allocations pass through m3_core.c. Account and cap them,
# including allocation headers, without changing the application's allocator.
set_source_files_properties("${M3_DIR}/m3_core.c" PROPERTIES
    COMPILE_DEFINITIONS "calloc=demo_calloc;realloc=demo_realloc;free=demo_free")

add_executable(lightvm src/main.c src/app.c src/protocol.c src/vm_allocator.c)
target_include_directories(lightvm PRIVATE src)
target_compile_options(lightvm PRIVATE -Wall -Wextra -Werror)
target_link_libraries(lightvm PRIVATE wasm3 m)
if(LIGHTVM_PICO)
    target_compile_definitions(lightvm PRIVATE DEMO_PICO=1 PICO_STACK_SIZE=8192)
    target_link_libraries(lightvm PRIVATE pico_stdlib pico_stdio_usb)
    # SDK 1.5.1's ROM-v1 double shim is licensed for Pico devices only.
    # Use the compiler runtime so firmware avoids that restricted component.
    pico_set_double_implementation(lightvm compiler)
    pico_enable_stdio_usb(lightvm 1)
    pico_enable_stdio_uart(lightvm 0)
    pico_add_extra_outputs(lightvm)
endif()

# Keep redistribution notices beside the generated executables/firmware.
configure_file(LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE" COPYONLY)
configure_file(THIRD_PARTY_NOTICES.md "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.md" COPYONLY)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/licenses" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
