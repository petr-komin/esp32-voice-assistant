#!/usr/bin/env bash
# Firmware formatting and static analysis. Used by CI; runs locally if
# clang-format 19 and cppcheck are installed.
#
#   scripts/lint-firmware.sh format-check   # fail on formatting differences
#   scripts/lint-firmware.sh format         # rewrite files in place
#   scripts/lint-firmware.sh cppcheck
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../firmware"
mapfile -t sources < <(find main components test -name '*.c' -o -name '*.h' | sort)

case "${1:-}" in
format-check)
    clang-format --dry-run --Werror "${sources[@]}"
    ;;
format)
    clang-format -i "${sources[@]}"
    ;;
cppcheck)
    # Only project code is analysed; ESP-IDF headers are not on the include path,
    # so missing system includes are expected and suppressed.
    cppcheck --error-exitcode=1 --std=c11 --inline-suppr --quiet \
        --enable=warning,style,performance,portability \
        --suppress=missingIncludeSystem --suppress=unusedFunction \
        --suppress=missingInclude --suppress=unmatchedSuppression \
        -I components/dsp/include -I components/vad/include \
        -I components/audio/include -I components/status_led/include \
        main components test/host
    ;;
*)
    echo "usage: $0 format-check|format|cppcheck" >&2
    exit 2
    ;;
esac
