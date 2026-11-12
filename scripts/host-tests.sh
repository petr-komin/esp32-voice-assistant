#!/usr/bin/env bash
# Build and run the firmware host unit tests (ASan + UBSan) in the ESP-IDF container.
set -euo pipefail

IDF_IMAGE="${IDF_IMAGE:-espressif/idf:v6.0.3}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec docker run --rm -v "$root:/project" -w /project/firmware -u "$(id -u):$(id -g)" -e HOME=/tmp \
    "$IDF_IMAGE" bash -c '
        cmake -S test/host -B build-host -G Ninja >/dev/null &&
        cmake --build build-host &&
        ctest --test-dir build-host --output-on-failure'
