# Host (PC) unit tests for the hardware-independent components.
# Built with the host compiler, AddressSanitizer and UBSan; uses the Unity
# framework shipped with ESP-IDF, so IDF_PATH must be set.
#
#   cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
cmake_minimum_required(VERSION 3.22)
project(voice_terminal_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is not set (Unity is taken from ESP-IDF)")
endif()

set(UNITY_DIR $ENV{IDF_PATH}/components/unity/unity/src)
add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR})

set(COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../components)
set(SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
set(STRICT -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Wconversion)

enable_testing()

function(host_test name)
    cmake_parse_arguments(T "" "" "SOURCES;INCLUDES" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_include_directories(${name} PRIVATE ${T_INCLUDES})
    target_compile_options(${name} PRIVATE ${STRICT} ${SANITIZERS} -g)
    target_link_options(${name} PRIVATE ${SANITIZERS})
    target_link_libraries(${name} PRIVATE unity m)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

host_test(test_dsp
    SOURCES test_dsp.c ${COMPONENTS}/dsp/dsp.c
    INCLUDES ${COMPONENTS}/dsp/include)

host_test(test_vad
    SOURCES test_vad.c ${COMPONENTS}/vad/vad.c
    INCLUDES ${COMPONENTS}/vad/include)
