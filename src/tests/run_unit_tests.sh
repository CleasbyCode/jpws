#!/bin/bash
# Unit tests for jpws helpers (segment bounds, tail-retry status, libjpeg inspector).
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
WORKDIR="$(mktemp -d)"
OUT="$WORKDIR/jpws_unit_tests"
RUN_LOG="$WORKDIR/run.log"

cleanup() {
    local status=$?
    if [[ -n "${WORKDIR:-}" && -d "$WORKDIR" ]]; then
        rm -rf -- "$WORKDIR"
    fi
    return "$status"
}
trap cleanup EXIT

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd g++

g++ -std=c++23 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT" \
    "$TESTS/unit/test_jpws.cpp" \
    "$ROOT/jpeg_process.cpp" \
    "$ROOT/jpeg_warning_check.cpp" \
    -lturbojpeg -ljpeg \
    -o "$OUT"

set +e
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$OUT" >"$RUN_LOG" 2>&1
test_rc=$?
set -e

if [[ "$test_rc" -eq 0 ]]; then
    cat "$RUN_LOG"
elif grep -q "LeakSanitizer has encountered a fatal error" "$RUN_LOG"; then
    cat "$RUN_LOG" >&2
    echo "LeakSanitizer is unavailable in this environment; retrying with leak detection disabled." >&2
    ASAN_OPTIONS="${ASAN_OPTIONS:-}:detect_leaks=0" "$OUT"
else
    cat "$RUN_LOG" >&2
    exit "$test_rc"
fi
echo "Unit tests passed."
