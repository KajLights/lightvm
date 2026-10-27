# Only wire/lease code is linked into the lighting controller. PCM analysis
# belongs to publishers; all hosts pin the same source of truth.
set(KAJ_AUDIO_CORE "$ENV{KAJ_AUDIO_CORE}" CACHE PATH "Pinned kaj-fi core directory")
if(NOT KAJ_AUDIO_CORE)
    set(KAJ_AUDIO_CORE "${LIGHTVM_ROOT}/.deps/kaj-fi/core")
endif()
if(NOT EXISTS "${KAJ_AUDIO_CORE}/subscriber.c")
    message(FATAL_ERROR "Run python3 tools/fetch_audio_core.py or set KAJ_AUDIO_CORE to the pinned kaj-fi core directory")
endif()
file(STRINGS "${LIGHTVM_ROOT}/kaj-audio.rev" KAJ_AUDIO_REV LIMIT_COUNT 1)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${LIGHTVM_ROOT}/tools/audio_dependency.py" --verify "${KAJ_AUDIO_CORE}"
    OUTPUT_VARIABLE KAJ_AUDIO_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE KAJ_AUDIO_VERIFY_ERROR)
if(KAJ_AUDIO_VERIFY_ERROR OR NOT KAJ_AUDIO_HEAD STREQUAL KAJ_AUDIO_REV)
    message(FATAL_ERROR "kaj-fi source bytes must match kaj-audio.rev (${KAJ_AUDIO_REV})")
endif()
