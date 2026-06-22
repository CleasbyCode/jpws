#!/bin/bash
# Verify-only golden-file regression tests for jpws.
#
# Each golden/ case ships a pre-built JPG-PowerShell polyglot and the script it
# embeds. The test re-verifies the committed polyglot's structure and that the
# script is still embedded verbatim (a recover-style roundtrip) WITHOUT rebuilding
# it — so the fixtures stay valid across libjpeg/turbojpeg versions. This catches
# regressions in the polyglot layout (JFIF comment block, ICC FFE2 segment,
# per-option tail patch) and the embed path.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
GOLDEN="$TESTS/golden"
MANIFEST="$GOLDEN/manifest.tsv"

usage() {
    cat <<'EOF'
Usage: tests/run_golden_tests.sh [options]

Options:
  -h, --help   Show this help.

Notes:
  Verifies committed golden polyglots only; it does not build or run jpws.
  Regenerate fixtures with: bash tests/generate_golden.sh
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1" >&2; usage; exit 2;;
    esac
done

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd python3
need_cmd ffmpeg
need_cmd stat

if [[ ! -f "$MANIFEST" ]]; then
    echo "Missing golden manifest: $MANIFEST" >&2
    echo "Generate fixtures with: bash tests/generate_golden.sh" >&2
    exit 1
fi

# Assert the polyglot structure for `option` (default|alt): JPEG SOI/EOI, the
# JFIF comment block "XTW\n<#" at 0x0C, the ICC "_jpws_" marker, the script
# embedded verbatim, "#>" within the last 256 bytes, and the exact 10-byte tail
# patch (which differs between default and -alt) just before EOI.
assert_polyglot_structure() {
    local poly="$1" script="$2" option="$3" tag="$4"
    if ! JPWS_POLY="$poly" JPWS_SCRIPT="$script" JPWS_OPTION="$option" python3 - <<'PY'
import os, sys
from pathlib import Path

d = Path(os.environ["JPWS_POLY"]).read_bytes()
script = Path(os.environ["JPWS_SCRIPT"]).read_bytes()
option = os.environ["JPWS_OPTION"]

# Scripts are stripped of a leading UTF-8 BOM by jpws before embedding.
if script[:3] == b"\xEF\xBB\xBF":
    script = script[3:]

if d[:2] != b"\xFF\xD8":
    sys.exit("missing SOI marker")
if d[-2:] != b"\xFF\xD9":
    sys.exit("missing EOI marker")

JFIF_COMMENT = bytes([0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23])  # "XTW\n<#"
if d[0x0C:0x0C + len(JFIF_COMMENT)] != JFIF_COMMENT:
    sys.exit("JFIF comment block not at 0x0C")

if bytes([0x5F, 0x6A, 0x70, 0x77, 0x73, 0x5F]) not in d:  # "_jpws_"
    sys.exit("ICC _jpws_ marker missing")

if script not in d:
    sys.exit("embedded script not found verbatim (roundtrip failed)")

if bytes([0x23, 0x3E]) not in d[-256:]:  # "#>"
    sys.exit("close-comment '#>' not in tail window")

DEFAULT_TAIL = bytes([0x9E, 0x23, 0x3E, 0x0D, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00])
ALT_TAIL     = bytes([0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9E])
expected = ALT_TAIL if option == "alt" else DEFAULT_TAIL
if d[-12:-2] != expected:
    sys.exit(f"tail patch mismatch for option={option}: got {d[-12:-2].hex()}")
PY
    then
        echo "[FAIL] $tag: polyglot structure check failed" >&2
        return 1
    fi
    return 0
}

PASS=0
FAIL=0

run_case() {
    local case_id="$1" option="$2" script_rel="$3" golden_rel="$4"
    local script="$TESTS/$script_rel"
    local golden="$TESTS/$golden_rel"

    [[ "$option" == "." ]] && option=""
    local opt_label="${option:+alt}"
    [[ -z "$opt_label" ]] && opt_label="default"

    if [[ ! -f "$script" ]]; then
        echo "[FAIL] $case_id: missing script $script_rel" >&2
        return 1
    fi
    if [[ ! -f "$golden" ]]; then
        echo "[FAIL] $case_id: missing golden polyglot $golden_rel (run generate_golden.sh)" >&2
        return 1
    fi

    assert_polyglot_structure "$golden" "$script" "$opt_label" "$case_id" || return 1

    # Independent decode confirms the polyglot is still a valid JPEG image.
    if ! ffmpeg -hide_banner -loglevel error -i "$golden" -f null - >/dev/null 2>&1; then
        echo "[FAIL] $case_id: golden polyglot does not decode as a valid JPEG" >&2
        return 1
    fi

    echo "[PASS] $case_id"
    return 0
}

while IFS=$'\t' read -r case_id option script_rel golden_rel; do
    [[ -z "$case_id" || "$case_id" == "case_id" ]] && continue
    if run_case "$case_id" "$option" "$script_rel" "$golden_rel"; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done < "$MANIFEST"

echo
echo "Golden test summary: PASS=$PASS FAIL=$FAIL"

if [[ "$FAIL" -ne 0 ]]; then
    exit 1
fi
