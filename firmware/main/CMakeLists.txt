idf_component_register(SRCS "main.c" "echo.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES audio dsp vad status_led esp_app_format esp_system)

target_compile_options(${COMPONENT_LIB} PRIVATE ${VT_STRICT_WARNINGS})
