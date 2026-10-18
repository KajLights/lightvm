target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/platform.c")

if(LIGHTVM_MQTT)
    find_package(Threads REQUIRED)
    target_sources(lightvm PRIVATE "${CMAKE_SOURCE_DIR}/src/mqtt_posix.c")
    target_link_libraries(lightvm PRIVATE Threads::Threads)
endif()
if(LIGHTVM_HTTP)
    target_sources(lightvm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/http_server.c")
endif()
