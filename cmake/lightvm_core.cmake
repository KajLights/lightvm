# Reuse this build from a native SDK project without the host/Pico executable.
# The caller sets LIGHTVM_BOARD_CONFIG and may set WASM3_SOURCE_DIR.
function(lightvm_add_core)
    get_filename_component(LIGHTVM_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    if(NOT EXISTS "${LIGHTVM_BOARD_CONFIG}")
        message(FATAL_ERROR "Set LIGHTVM_BOARD_CONFIG to the port's board/resource header")
    endif()
    add_library(lightvm_config INTERFACE)
    target_include_directories(lightvm_config INTERFACE "${LIGHTVM_ROOT}/src")
    target_compile_definitions(lightvm_config INTERFACE
        LIGHTVM_BOARD_CONFIG="${LIGHTVM_BOARD_CONFIG}")

    set(WASM3_SOURCE_DIR "${WASM3_SOURCE_DIR}" CACHE PATH "Optional existing wasm3 v0.5.0 checkout")
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
    # wasm3 intentionally aliases its typed code/stack slots. Keep optimizers
    # from applying incompatible strict-aliasing assumptions on any native port.
    target_compile_options(wasm3 PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-fno-strict-aliasing>)
    target_include_directories(wasm3 PUBLIC "${M3_DIR}")
    target_compile_definitions(wasm3 PRIVATE
        d_m3MaxLinearMemoryPages=1 d_m3MaxFunctionStackHeight=128
        d_m3CodePageAlignSize=1024 d_m3LogOutput=0)
    # All interpreter allocations pass through m3_core.c. Account and cap them,
    # including allocation headers, without changing the application's allocator.
    set_source_files_properties("${M3_DIR}/m3_core.c" PROPERTIES
        COMPILE_DEFINITIONS "calloc=demo_calloc;realloc=demo_realloc;free=demo_free")

    add_library(lightvm_core STATIC
        "${LIGHTVM_ROOT}/src/app.c" "${LIGHTVM_ROOT}/src/protocol.c"
        "${LIGHTVM_ROOT}/src/vm_allocator.c" "${LIGHTVM_ROOT}/src/pixels.c"
        "${LIGHTVM_ROOT}/src/session.c" "${LIGHTVM_ROOT}/src/vm.c"
        "${LIGHTVM_ROOT}/src/json.c" "${LIGHTVM_ROOT}/src/devices.c"
        "${LIGHTVM_ROOT}/src/store.c")
    target_compile_features(lightvm_core PUBLIC c_std_11)
    target_compile_options(lightvm_core PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(lightvm_core PUBLIC lightvm_config wasm3 m)
endfunction()
