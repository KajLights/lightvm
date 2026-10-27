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
execute_process(COMMAND git -C "${KAJ_AUDIO_CORE}" rev-parse HEAD OUTPUT_VARIABLE KAJ_AUDIO_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE KAJ_AUDIO_GIT_ERROR)
if(KAJ_AUDIO_GIT_ERROR OR NOT KAJ_AUDIO_HEAD STREQUAL KAJ_AUDIO_REV)
    message(FATAL_ERROR "kaj-fi dependency must match kaj-audio.rev (${KAJ_AUDIO_REV}); got ${KAJ_AUDIO_HEAD}")
endif()
