cmake_minimum_required(VERSION 3.22)

# Warnings applied to every project component (ESP-IDF's own code keeps its defaults).
# -Wdouble-promotion catches accidental double math: the ESP32 FPU is single precision only.
set(VT_STRICT_WARNINGS
    -Wall -Wextra -Werror
    -Wshadow -Wdouble-promotion -Wformat=2 -Wstrict-prototypes)
# -Wundef is not used: ESP-IDF headers rely on undefined macros evaluating to 0.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(voice_terminal)
