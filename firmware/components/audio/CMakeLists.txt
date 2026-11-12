idf_component_register(SRCS "audio.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2s
                       PRIV_REQUIRES dsp)

target_compile_options(${COMPONENT_LIB} PRIVATE ${VT_STRICT_WARNINGS})
