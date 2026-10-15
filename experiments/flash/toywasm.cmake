# Existing interpreter, pinned independently of the default wasm3 backend.
function(lightvm_add_toywasm)
    if(LIGHTVM_PLATFORM STREQUAL "pico")
        message(FATAL_ERROR "Flash experiment currently supports host and ESP32")
    endif()
    set(TOYWASM_SOURCE_DIR "${TOYWASM_SOURCE_DIR}" CACHE PATH "Pinned Toywasm source")
    if(NOT TOYWASM_SOURCE_DIR)
        include(FetchContent)
        FetchContent_Declare(toywasm GIT_REPOSITORY https://github.com/yamt/toywasm.git
            GIT_TAG 964e182c1a1b09694f9f8f87c32fd26ba8b15e34)
        FetchContent_GetProperties(toywasm)
        if(NOT toywasm_POPULATED)
            FetchContent_Populate(toywasm)
        endif()
        set(TOYWASM_SOURCE_DIR "${toywasm_SOURCE_DIR}")
    endif()
    set(TW "${CMAKE_CURRENT_BINARY_DIR}/toywasm-source")
    file(MAKE_DIRECTORY "${TW}")
    file(COPY "${TOYWASM_SOURCE_DIR}/lib/" DESTINATION "${TW}")
    # Bounded checks use LightVM's existing deadline. The interpreter itself is
    # upstream; this hook is the only execution-path modification.
    file(READ "${TW}/exec.c" EXEC)
    string(REPLACE "check_interrupt(struct exec_context *ctx)\n{"
        "check_interrupt(struct exec_context *ctx)\n{\n        extern int lightvm_toy_interrupt(void);\n        if (lightvm_toy_interrupt()) return ETOYWASMUSERINTERRUPT;" EXEC "${EXEC}")
    file(WRITE "${TW}/exec.c" "${EXEC}")
    file(READ "${TW}/mem.c" MEM)
    string(REPLACE "!defined(NDEBUG) && defined(__APPLE__)" "0 /* allocator has its own header */" MEM "${MEM}")
    file(WRITE "${TW}/mem.c" "${MEM}")
    # Use the project's allocator for every runtime allocation, including reports.
    set(TOYWASM_USE_SEPARATE_EXECUTE ON)
    set(TOYWASM_USE_SEPARATE_VALIDATE OFF)
    set(TOYWASM_PROCESS_INSN_WITH_SWITCH ON)
    set(TOYWASM_USE_SEPARATE_LOCALS ON)
    set(TOYWASM_USE_SMALL_CELLS ON)
    set(TOYWASM_USE_RESULTTYPE_CELLIDX ON)
    set(TOYWASM_USE_LOCALTYPE_CELLIDX ON)
    set(TOYWASM_USE_LOCALS_FAST_PATH ON)
    set(TOYWASM_USE_LOCALS_CACHE ON)
    set(TOYWASM_USE_JUMP_BINARY_SEARCH ON)
    set(TOYWASM_ENABLE_HEAP_TRACKING ON)
    set(TOYWASM_ENABLE_HEAP_TRACKING_PEAK ON)
    set(TOYWASM_JUMP_CACHE2_SIZE 4)
    set(TOYWASM_EXCEPTION_MAX_CELLS 4)
    set(TOYWASM_VERSION "964e182c1a1b")
    configure_file("${TW}/toywasm_config.h.in" "${TW}/toywasm_config.h")
    configure_file("${TW}/toywasm_version.h.in" "${TW}/toywasm_version.h")
    configure_file("${TW}/toywasm_config.c.in" "${TW}/toywasm_config.c")
    set(TW_FILES bitmap cconv cell context decode dummy_host_func endian escape
        exec exec_debug exec_insn_subr expr expr_parser host_instance idalloc
        import_object insn instance leb128 list load_context mem module name
        options report restart shared_memory slist timeutil type util
        validation vec xlog toywasm_config)
    list(TRANSFORM TW_FILES PREPEND "${TW}/")
    list(TRANSFORM TW_FILES APPEND ".c")
    # Keep the existing link target name; no wasm3 source is included in this build.
    add_library(wasm3 STATIC ${TW_FILES} "${LIGHTVM_ROOT}/experiments/flash/adapter.c")
    target_include_directories(wasm3 PUBLIC "${LIGHTVM_ROOT}/experiments/flash/compat"
        PRIVATE "${TW}" "${LIGHTVM_ROOT}/src")
    target_link_libraries(wasm3 PRIVATE lightvm_config)
    target_compile_definitions(wasm3 PRIVATE read_u32=toywasm_read_u32
        mem_free=toywasm_mem_free mem_calloc=toywasm_mem_calloc)
    target_compile_options(wasm3 PRIVATE -fno-strict-aliasing -Wno-unused-variable
        -Wno-unused-but-set-variable -Wno-unknown-pragmas
        $<$<C_COMPILER_ID:GNU>:-Wno-maybe-uninitialized;-Wno-format>)
    option(LIGHTVM_TOY_JUMP_TABLES "Allow jump tables in the flash-resident Toywasm switch dispatcher" OFF)
    if(LIGHTVM_TOY_JUMP_TABLES)
        target_compile_options(wasm3 PRIVATE $<$<C_COMPILER_ID:GNU>:-fjump-tables;-ftree-switch-conversion>)
    endif()
    option(LIGHTVM_TOY_ASSERTIONS "Keep Toywasm internal debug assertions" ON)
    if(NOT LIGHTVM_TOY_ASSERTIONS)
        target_compile_definitions(wasm3 PRIVATE NDEBUG)
    endif()
    set_source_files_properties(${TW_FILES} PROPERTIES COMPILE_OPTIONS
        "-include;${LIGHTVM_ROOT}/experiments/flash/alloc.h")
endfunction()
