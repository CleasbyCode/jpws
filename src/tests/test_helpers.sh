#!/bin/bash
# Shared helpers for jpws shell tests. This file is intended to be sourced.

JPWS_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JPWS_ROOT_DIR="$(cd "$JPWS_TESTS_DIR/.." && pwd)"

jpws_need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        return 1
    fi
}

# Resolve an environment override before a test changes directory. Relative
# paths are relative to the directory from which the test was invoked.
jpws_resolve_from() {
    local base="$1"
    local path="$2"

    if [[ "$path" == /* ]]; then
        printf '%s\n' "$path"
    else
        printf '%s/%s\n' "$base" "$path"
    fi
}

# Build the application under a caller-owned temporary directory. Keeping this
# command here prevents tests from accidentally exercising a checked-in binary.
jpws_build_test_binary() {
    local output="$1"

    jpws_need_cmd g++ || return 1
    if [[ ! -d "$(dirname "$output")" ]]; then
        echo "Build output directory does not exist: $(dirname "$output")" >&2
        return 1
    fi

    g++ -std=c++23 -O2 -pipe \
        -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wformat-security \
        -Wconversion -Wsign-conversion \
        -I"$JPWS_ROOT_DIR" \
        "$JPWS_ROOT_DIR/args.cpp" \
        "$JPWS_ROOT_DIR/file_utils.cpp" \
        "$JPWS_ROOT_DIR/jpeg_process.cpp" \
        "$JPWS_ROOT_DIR/jpeg_warning_check.cpp" \
        "$JPWS_ROOT_DIR/jpws.cpp" \
        -lturbojpeg -ljpeg -o "$output"
}

jpws_extract_output_image() {
    sed -n 's/.*polyglot image: \([^ ]*\) (.*/\1/p' "$1" | tail -n 1
}

# Reject malformed, empty, and header-only manifests before a shell loop can
# silently turn them into a zero-test success.
jpws_validate_manifest() {
    local manifest="$1"

    JPWS_MANIFEST="$manifest" python3 - <<'PY'
import os
import re
import sys
from pathlib import Path, PurePosixPath

manifest = Path(os.environ["JPWS_MANIFEST"])
try:
    text = manifest.read_text(encoding="utf-8")
except (OSError, UnicodeError) as exc:
    sys.exit(f"Unable to read golden manifest {manifest}: {exc}")

lines = text.splitlines()
expected_header = ["case_id", "option", "script_rel", "golden_rel"]
if not lines:
    sys.exit("Golden manifest is empty")
if lines[0].split("\t") != expected_header:
    sys.exit("Golden manifest has an invalid header")
if len(lines) == 1:
    sys.exit("Golden manifest contains no test cases")

expected_rows = {
    "default_hello": (
        ".",
        "testdata/scripts/script_hello.ps1",
        "golden/default_hello/polyglot.jpg",
    ),
    "alt_hello": (
        "-alt",
        "testdata/scripts/script_hello.ps1",
        "golden/alt_hello/polyglot.jpg",
    ),
    "default_block": (
        ".",
        "testdata/scripts/script_block.ps1",
        "golden/default_block/polyglot.jpg",
    ),
    "alt_block": (
        "-alt",
        "testdata/scripts/script_block.ps1",
        "golden/alt_block/polyglot.jpg",
    ),
}
actual_rows = {}
for line_number, line in enumerate(lines[1:], start=2):
    if not line:
        sys.exit(f"Golden manifest has a blank row at line {line_number}")
    fields = line.split("\t")
    if len(fields) != 4 or any(not field for field in fields):
        sys.exit(f"Golden manifest has a malformed row at line {line_number}")

    case_id, option, script_rel, golden_rel = fields
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", case_id):
        sys.exit(f"Golden manifest has an unsafe case id at line {line_number}")
    if case_id in actual_rows:
        sys.exit(f"Golden manifest repeats case id {case_id!r}")
    if option not in (".", "-alt"):
        sys.exit(f"Golden manifest has an invalid option at line {line_number}")

    for label, value in (("script", script_rel), ("golden", golden_rel)):
        path = PurePosixPath(value)
        if path.is_absolute() or ".." in path.parts or "." in path.parts:
            sys.exit(
                f"Golden manifest has an unsafe {label} path at line {line_number}"
            )
    actual_rows[case_id] = (option, script_rel, golden_rel)

if actual_rows != expected_rows:
    missing = sorted(expected_rows.keys() - actual_rows.keys())
    unexpected = sorted(actual_rows.keys() - expected_rows.keys())
    changed = sorted(
        case_id
        for case_id in expected_rows.keys() & actual_rows.keys()
        if expected_rows[case_id] != actual_rows[case_id]
    )
    details = []
    if missing:
        details.append(f"missing cases: {', '.join(missing)}")
    if unexpected:
        details.append(f"unexpected cases: {', '.join(unexpected)}")
    if changed:
        details.append(f"changed mappings: {', '.join(changed)}")
    sys.exit("Golden manifest does not match the expected case set (" + "; ".join(details) + ")")
PY
}

# Assert the byte-level JPG/PowerShell layout and script roundtrip. The final
# EOI check is deliberately strict: bytes after EOI are not a valid jpws tail.
jpws_assert_polyglot_structure() {
    local poly="$1"
    local script="$2"
    local option="$3"

    JPWS_POLY="$poly" JPWS_SCRIPT="$script" JPWS_OPTION="$option" python3 - <<'PY'
import os
import sys
from pathlib import Path

try:
    data = Path(os.environ["JPWS_POLY"]).read_bytes()
    script = Path(os.environ["JPWS_SCRIPT"]).read_bytes()
except OSError as exc:
    sys.exit(str(exc))

option = os.environ["JPWS_OPTION"]
if option in ("", ".", "default"):
    option = "default"
elif option in ("-alt", "alt"):
    option = "alt"
else:
    sys.exit(f"unknown jpws option {option!r}")

# jpws strips a leading UTF-8 BOM before embedding.
if script.startswith(b"\xEF\xBB\xBF"):
    script = script[3:]

if data[:2] != b"\xFF\xD8":
    sys.exit("missing SOI marker")
if data[-2:] != b"\xFF\xD9":
    sys.exit("missing final EOI marker")

jfif_comment = b"XTW\n<#"
if data[0x0C:0x0C + len(jfif_comment)] != jfif_comment:
    sys.exit("JFIF comment block not at 0x0C")

if b"_jpws_" not in data:
    sys.exit("ICC _jpws_ marker missing")
if script not in data:
    sys.exit("embedded script not found verbatim (roundtrip failed)")
if b"#>" not in data[-256:]:
    sys.exit("close-comment '#>' not in tail window")

default_tail = bytes([0x9E, 0x23, 0x3E, 0x0D, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00])
alt_tail = bytes([0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9E])
expected = alt_tail if option == "alt" else default_tail
if data[-12:-2] != expected:
    sys.exit(f"tail patch mismatch for option={option}: got {data[-12:-2].hex()}")
PY
}

jpws_assert_jpeg_decodes() {
    ffmpeg -hide_banner -loglevel error -i "$1" -f null - >/dev/null 2>&1
}
