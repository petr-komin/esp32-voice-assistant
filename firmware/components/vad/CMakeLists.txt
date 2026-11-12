idf_component_register(SRCS "vad.c"
                       INCLUDE_DIRS "include")

target_compile_options(${COMPONENT_LIB} PRIVATE ${VT_STRICT_WARNINGS})
