#!/usr/bin/env bash
# Run idf.py in the pinned ESP-IDF container, so every machine (and CI) builds
# with exactly the same toolchain. Serial ports present on the host are passed in.
#
#   scripts/idf.sh build
#   scripts/idf.sh -p /dev/ttyUSB0 flash
#   scripts/idf.sh menuconfig
#
# While flashing, a pause flag tells scripts/monitor.py (running in another
# window) to release the serial port; it reconnects when flashing is done.
set -euo pipefail

IDF_IMAGE="${IDF_IMAGE:-espressif/idf:v6.0.3}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

args=(--rm -v "$root:/project" -w /project/firmware
      -u "$(id -u):$(id -g)" -e HOME=/tmp -e IDF_CCACHE_ENABLE=1
      -e CCACHE_DIR=/project/firmware/build/.ccache)
if [ -t 0 ] && [ -t 1 ]; then
    args+=(-it)
fi
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] && args+=(--device "$dev")
done

# Which serial port, and does this invocation touch it?
port=/dev/ttyUSB0
uses_port=false
prev=""
for a in "$@"; do
    case "$prev" in -p|--port) port="$a" ;; esac
    case "$a" in
        --port=*) port="${a#--port=}" ;;
        flash|app-flash|bootloader-flash|partition-table-flash|erase-flash|erase-otadata|monitor)
            uses_port=true ;;
    esac
    prev="$a"
done

if ! $uses_port; then
    exec docker run "${args[@]}" "$IDF_IMAGE" idf.py "$@"
fi

flag="${XDG_RUNTIME_DIR:-/tmp}/esp-serial-$(basename "$port").pause"
touch "$flag"
trap 'rm -f "$flag"' EXIT
sleep 0.3 # give the monitor time to close the port
docker run "${args[@]}" "$IDF_IMAGE" idf.py "$@"
