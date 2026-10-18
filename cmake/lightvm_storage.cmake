# Shared by host/Pico and the ESP-IDF project. Keep region size and linker /
# partition generation in the same build configuration.
set(LIGHTVM_STORAGE_BYTES "524288" CACHE STRING "Program storage region size in bytes (multiple of 8192)")
math(EXPR LIGHTVM_STORAGE_BYTES_VALUE "${LIGHTVM_STORAGE_BYTES}")
math(EXPR LIGHTVM_STORAGE_REMAINDER "${LIGHTVM_STORAGE_BYTES_VALUE} % 8192")
if(LIGHTVM_STORAGE_BYTES_VALUE LESS 16384 OR LIGHTVM_STORAGE_BYTES_VALUE GREATER 2147483647 OR NOT LIGHTVM_STORAGE_REMAINDER EQUAL 0)
    message(FATAL_ERROR "LIGHTVM_STORAGE_BYTES must be 16384..2147483647 and a multiple of 8192")
endif()
set(LIGHTVM_STORAGE_BACKEND "store_extent_backend" CACHE STRING "StoreBackend symbol selected by store_mount")
set(LIGHTVM_STORAGE_CONFIGURATION "" CACHE STRING "Optional const void *const symbol holding backend configuration")
set(LIGHTVM_STORAGE_BACKEND_SOURCES "" CACHE STRING "Additional source files implementing a custom StoreBackend")
