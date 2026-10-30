function(device_generate_build root product version board platform features flash_bytes)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    string(JOIN "," feature_ids ${features})
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/device-build")
    set(sdk_root "${PICO_SDK_PATH}")
    if(DEFINED ENV{IDF_PATH})
        set(sdk_root "$ENV{IDF_PATH}")
    endif()
    add_custom_target(device_build_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/build_metadata.py"
            --root "${root}" --core "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.."
            --product "${product}" --version "${version}" --board "${board}" --platform "${platform}"
            --features "${feature_ids}" --flash-bytes "${flash_bytes}"
            --cache "${CMAKE_BINARY_DIR}/CMakeCache.txt" --sdkconfig "${SDKCONFIG}"
            --layout "${LIGHTVM_PARTITION_FILENAME}" --compiler "${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}"
            --build-type "${CMAKE_BUILD_TYPE}" --sdk-root "${sdk_root}"
            --output "${generated}"
        BYPRODUCTS "${generated}/build.c" "${generated}/build.json"
        VERBATIM)
    set_source_files_properties("${generated}/build.c" TARGET_DIRECTORY device_core PROPERTIES GENERATED TRUE)
    target_sources(device_core PRIVATE "${generated}/build.c")
    add_dependencies(device_core device_build_metadata)
endfunction()
