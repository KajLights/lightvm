idf_component_register(SRCS "probe.c" "${KAJ_AUDIO_CORE}/features.c" "${KAJ_AUDIO_CORE}/onset.c" "${KAJ_FI_ROOT}/main/fft.c"
    INCLUDE_DIRS "${KAJ_AUDIO_CORE}" "${KAJ_FI_ROOT}/main"
    PRIV_REQUIRES esp_timer esp-dsp esp_driver_usb_serial_jtag esp_hw_support)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror -Wdouble-promotion)
