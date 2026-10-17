# Shared by the portable core and SDK metadata; no independent release counters.
get_filename_component(LIGHTVM_RELEASE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LIGHTVM_RELEASE_ROOT}/lightvm/release.json")
file(READ "${LIGHTVM_RELEASE_ROOT}/lightvm/release.json" LIGHTVM_RELEASE_JSON)
string(REGEX MATCH "\"version\"[ \t]*:[ \t]*\"([^\"]+)\"" LIGHTVM_RELEASE_MATCH "${LIGHTVM_RELEASE_JSON}")
if(NOT LIGHTVM_RELEASE_MATCH)
    message(FATAL_ERROR "Missing application version in lightvm/release.json")
endif()
set(LIGHTVM_VERSION "${CMAKE_MATCH_1}")
