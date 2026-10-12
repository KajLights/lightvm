target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/platform.c")

if(LIGHTVM_HTTP)
    target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/http_server.c")
endif()
