#!/bin/bash
# CLI regression tests for filename validation and script-size limits.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INVOKED_FROM="$(pwd -P)"
COVER="$TESTS/testdata/covers/cover.jpg"

# shellcheck source=tests/test_helpers.sh
source "$TESTS/test_helpers.sh"

jpws_need_cmd python3
jpws_need_cmd mkfifo
jpws_need_cmd timeout

if [[ ! -f "$COVER" ]]; then
    echo "Missing cover fixture: $COVER" >&2
    exit 1
fi

WORKDIR="$(mktemp -d)"
cleanup() {
    local status=$?
    if [[ -n "${WORKDIR:-}" && -d "$WORKDIR" ]]; then
        rm -rf -- "$WORKDIR"
    fi
    return "$status"
}
trap cleanup EXIT

# JPWS is an explicit escape hatch for testing a particular executable. The
# normal path always compiles the current sources into the temporary directory.
if [[ -n "${JPWS:-}" ]]; then
    JPWS="$(jpws_resolve_from "$INVOKED_FROM" "$JPWS")"
else
    JPWS="$WORKDIR/jpws"
    echo "Building current jpws sources for CLI tests..."
    jpws_build_test_binary "$JPWS"
fi
if [[ ! -x "$JPWS" ]]; then
    echo "Missing jpws executable: $JPWS" >&2
    exit 1
fi

cd "$WORKDIR"

PASS=0
FAIL=0

pass() {
    echo "[PASS] $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "[FAIL] $1" >&2
    FAIL=$((FAIL + 1))
}

# --info advertises the exact FFE2-limited script cap, not "~10KB".
info_out="$("$JPWS" --info)"
if echo "$info_out" | grep -q "Max script size is 9812 bytes."; then
    pass "info_script_size"
else
    fail "info_script_size: unexpected --info text"
fi

# 9813 bytes passes the old 10KB file check and then dies in validateProfileSizes.
python3 - <<'PY'
from pathlib import Path
Path("too_big.ps1").write_bytes(b"A" * 9813)
Path("just_right.ps1").write_bytes(b"A" * 9812)
PY
cp "$COVER" cover.jpg

if err="$("$JPWS" cover.jpg too_big.ps1 2>&1)"; then
    fail "oversized_script: jpws accepted a 9813-byte script"
elif echo "$err" | grep -q "PowerShell script exceeds maximum size limit"; then
    pass "oversized_script"
else
    fail "oversized_script: wrong error: $err"
fi

# Spaces and parentheses are valid filename characters.
cp "$COVER" "My Photo.jpg"
printf 'Write-Host hello-from-space-name\n' > "script (1).ps1"
set +e
space_out="$("$JPWS" "My Photo.jpg" "script (1).ps1" 2>&1)"
space_rc=$?
set -e
if echo "$space_out" | grep -q "Unsupported characters in filename arguments"; then
    fail "filename_spaces: still rejected by charset check"
elif [[ "$space_rc" -eq 0 ]]; then
    pass "filename_spaces"
else
    fail "filename_spaces: rejected for a different reason: $space_out"
fi

# Exact payload cap must still be accepted by the file-size check.
set +e
cap_out="$("$JPWS" cover.jpg just_right.ps1 2>&1)"
cap_rc=$?
set -e
if echo "$cap_out" | grep -q "PowerShell script exceeds maximum size limit"; then
    fail "exact_script_cap: 9812-byte script rejected as too large"
elif echo "$cap_out" | grep -q "profile segment (FFE2) exceeds"; then
    fail "exact_script_cap: 9812-byte script still hits FFE2 limit"
elif [[ "$cap_rc" -eq 0 ]]; then
    pass "exact_script_cap"
else
    fail "exact_script_cap: unexpected error: $cap_out"
fi

# Opening a FIFO must not block before the regular-file check.
mkfifo script_fifo.ps1
set +e
fifo_out="$(timeout 2s "$JPWS" cover.jpg script_fifo.ps1 2>&1)"
fifo_rc=$?
set -e
if [[ "$fifo_rc" -eq 124 ]]; then
    fail "fifo_input: jpws blocked while opening a named pipe"
elif [[ "$fifo_rc" -eq 0 ]]; then
    fail "fifo_input: jpws accepted a named pipe"
elif echo "$fifo_out" | grep -q "is not a regular file"; then
    pass "fifo_input"
else
    fail "fifo_input: wrong error: $fifo_out"
fi

echo
echo "CLI test summary: PASS=$PASS FAIL=$FAIL"
if [[ "$FAIL" -ne 0 ]]; then
    exit 1
fi
