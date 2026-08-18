#!/bin/bash
# Golden-layout verification and fresh-build integration tests for jpws.
#
# Committed golden files remain useful across encoder versions. By default this
# script also builds the current sources in a temporary directory, generates a
# new output for every manifest case, and applies the same structural checks.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INVOKED_FROM="$(pwd -P)"
GOLDEN="$TESTS/golden"
MANIFEST="$GOLDEN/manifest.tsv"
VERIFY_ONLY=0

# shellcheck source=tests/test_helpers.sh
source "$TESTS/test_helpers.sh"

usage() {
    cat <<'EOF'
Usage: tests/run_golden_tests.sh [options]

Options:
  --verify-only   Verify committed golden files without building or running jpws.
  -h, --help      Show this help.

Environment:
  JPWS_BIN        Use this executable for integration cases instead of building
                  the current sources. A relative path is resolved from the
                  directory in which this script was invoked.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --verify-only) VERIFY_ONLY=1; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1" >&2; usage; exit 2;;
    esac
done

jpws_need_cmd python3
jpws_need_cmd ffmpeg

if [[ ! -f "$MANIFEST" ]]; then
    echo "Missing golden manifest: $MANIFEST" >&2
    echo "Generate fixtures with: bash tests/generate_golden.sh" >&2
    exit 1
fi
jpws_validate_manifest "$MANIFEST"

WORKDIR=""
cleanup() {
    local status=$?
    if [[ -n "${WORKDIR:-}" && -d "$WORKDIR" ]]; then
        rm -rf -- "$WORKDIR"
    fi
    return "$status"
}
trap cleanup EXIT

BIN=""
if [[ "$VERIFY_ONLY" -eq 0 ]]; then
    WORKDIR="$(mktemp -d)"
    if [[ -n "${JPWS_BIN:-}" ]]; then
        BIN="$(jpws_resolve_from "$INVOKED_FROM" "$JPWS_BIN")"
    else
        BIN="$WORKDIR/jpws"
        echo "Building current jpws sources for integration tests..."
        jpws_build_test_binary "$BIN"
    fi
    if [[ ! -x "$BIN" ]]; then
        echo "Missing jpws executable: $BIN" >&2
        exit 1
    fi
fi

check_polyglot() {
    local poly="$1"
    local script="$2"
    local option="$3"
    local tag="$4"

    if ! jpws_assert_polyglot_structure "$poly" "$script" "$option"; then
        echo "[FAIL] $tag: polyglot structure check failed" >&2
        return 1
    fi
    if ! jpws_assert_jpeg_decodes "$poly"; then
        echo "[FAIL] $tag: polyglot does not decode as a valid JPEG" >&2
        return 1
    fi
}

run_committed_case() {
    local case_id="$1"
    local option="$2"
    local script_rel="$3"
    local golden_rel="$4"
    local script="$TESTS/$script_rel"
    local golden="$TESTS/$golden_rel"

    if [[ ! -f "$script" ]]; then
        echo "[FAIL] committed/$case_id: missing script $script_rel" >&2
        return 1
    fi
    if [[ ! -f "$golden" ]]; then
        echo "[FAIL] committed/$case_id: missing $golden_rel (run generate_golden.sh)" >&2
        return 1
    fi
    if ! check_polyglot "$golden" "$script" "$option" "committed/$case_id"; then
        return 1
    fi

    echo "[PASS] committed/$case_id"
}

run_generated_case() {
    local case_id="$1"
    local option="$2"
    local script_rel="$3"
    local cover="$TESTS/testdata/covers/cover.jpg"
    local script="$TESTS/$script_rel"
    local case_work="$WORKDIR/$case_id"
    local run_log="$case_work/run.log"
    local output

    if [[ ! -f "$cover" ]]; then
        echo "[FAIL] generated/$case_id: missing cover fixture" >&2
        return 1
    fi
    if [[ ! -f "$script" ]]; then
        echo "[FAIL] generated/$case_id: missing script $script_rel" >&2
        return 1
    fi

    mkdir -p "$case_work"
    cp "$cover" "$case_work/cover.jpg"
    cp "$script" "$case_work/script.ps1"
    if [[ "$option" == "-alt" ]]; then
        if ! (cd "$case_work" && "$BIN" -alt cover.jpg script.ps1) >"$run_log" 2>&1; then
            echo "[FAIL] generated/$case_id: jpws exited unsuccessfully" >&2
            sed 's/^/    /' "$run_log" >&2
            return 1
        fi
    else
        if ! (cd "$case_work" && "$BIN" cover.jpg script.ps1) >"$run_log" 2>&1; then
            echo "[FAIL] generated/$case_id: jpws exited unsuccessfully" >&2
            sed 's/^/    /' "$run_log" >&2
            return 1
        fi
    fi

    output="$(jpws_extract_output_image "$run_log")"
    if [[ -z "$output" || "$output" != "$(basename -- "$output")" || ! -f "$case_work/$output" ]]; then
        echo "[FAIL] generated/$case_id: output image was not reported or created" >&2
        sed 's/^/    /' "$run_log" >&2
        return 1
    fi
    if ! check_polyglot "$case_work/$output" "$script" "$option" "generated/$case_id"; then
        return 1
    fi

    echo "[PASS] generated/$case_id"
}

COMMITTED_PASS=0
COMMITTED_FAIL=0
GENERATED_PASS=0
GENERATED_FAIL=0
CASE_COUNT=0

while IFS=$'\t' read -r case_id option script_rel golden_rel; do
    [[ "$case_id" == "case_id" ]] && continue
    CASE_COUNT=$((CASE_COUNT + 1))

    if run_committed_case "$case_id" "$option" "$script_rel" "$golden_rel"; then
        COMMITTED_PASS=$((COMMITTED_PASS + 1))
    else
        COMMITTED_FAIL=$((COMMITTED_FAIL + 1))
    fi

    if [[ "$VERIFY_ONLY" -eq 0 ]]; then
        if run_generated_case "$case_id" "$option" "$script_rel"; then
            GENERATED_PASS=$((GENERATED_PASS + 1))
        else
            GENERATED_FAIL=$((GENERATED_FAIL + 1))
        fi
    fi
done < "$MANIFEST"

# jpws_validate_manifest already enforces this; keep the local assertion so a
# future parser change cannot accidentally restore the zero-test false pass.
if [[ "$CASE_COUNT" -eq 0 ]]; then
    echo "Golden manifest produced zero test cases" >&2
    exit 1
fi

echo
echo "Committed golden summary: PASS=$COMMITTED_PASS FAIL=$COMMITTED_FAIL"
if [[ "$VERIFY_ONLY" -eq 0 ]]; then
    echo "Fresh-build integration summary: PASS=$GENERATED_PASS FAIL=$GENERATED_FAIL"
fi

if [[ "$COMMITTED_FAIL" -ne 0 || "$GENERATED_FAIL" -ne 0 ]]; then
    exit 1
fi
