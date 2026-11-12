idf_component_register(SRCS "status_led.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE ${VT_STRICT_WARNINGS})
