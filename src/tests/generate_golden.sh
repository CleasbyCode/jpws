#!/bin/bash
# Build jpws and transactionally regenerate committed golden fixtures.
#
# All cases are generated and validated in a staging directory. The existing
# golden directory is replaced only after every case has succeeded.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
INVOKED_FROM="$(pwd -P)"
GOLDEN="$TESTS/golden"
NO_BUILD=0

# shellcheck source=tests/test_helpers.sh
source "$TESTS/test_helpers.sh"

usage() {
    cat <<'EOF'
Usage: tests/generate_golden.sh [options]

Options:
  --no-build   Use JPWS_BIN, or ./jpws from the source root, without rebuilding.
  -h, --help   Show this help.

Environment:
  JPWS_BIN     Use this executable instead of building the current sources. A
               relative path is resolved from the invocation directory.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1" >&2; usage; exit 2;;
    esac
done

jpws_need_cmd python3
jpws_need_cmd ffmpeg

bash "$TESTS/create_testdata.sh"

# Staging beside golden guarantees that the final directory renames stay on one
# filesystem. The cleanup trap also restores the old directory if installation
# is interrupted between the two renames.
STAGE_ROOT="$(mktemp -d "$TESTS/.golden-stage.XXXXXX")"
STAGED_GOLDEN="$STAGE_ROOT/golden"
STAGED_MANIFEST="$STAGED_GOLDEN/manifest.tsv"
BACKUP_GOLDEN="$STAGE_ROOT/previous-golden"
HAD_GOLDEN=0
mkdir -p "$STAGED_GOLDEN"

cleanup() {
    local status=$?

    if [[ "$HAD_GOLDEN" -eq 1 && ! -e "$GOLDEN" && ! -L "$GOLDEN" && -d "$BACKUP_GOLDEN" ]]; then
        if ! mv "$BACKUP_GOLDEN" "$GOLDEN"; then
            echo "Unable to restore previous golden fixtures from: $BACKUP_GOLDEN" >&2
            status=1
        fi
    fi
    if [[ -n "${STAGE_ROOT:-}" && -d "$STAGE_ROOT" && "$STAGE_ROOT" == "$TESTS"/.golden-stage.* ]]; then
        rm -rf -- "$STAGE_ROOT"
    fi
    return "$status"
}
trap cleanup EXIT

if [[ -n "${JPWS_BIN:-}" ]]; then
    BIN="$(jpws_resolve_from "$INVOKED_FROM" "$JPWS_BIN")"
elif [[ "$NO_BUILD" -eq 1 ]]; then
    BIN="$ROOT/jpws"
else
    BIN="$STAGE_ROOT/jpws"
    echo "Building current jpws sources for golden generation..."
    jpws_build_test_binary "$BIN"
fi
if [[ ! -x "$BIN" ]]; then
    echo "Binary not found or not executable: $BIN" >&2
    exit 1
fi

# case_id  option(.|-alt)  script_rel
CASES=(
    $'default_hello\t.\ttestdata/scripts/script_hello.ps1'
    $'alt_hello\t-alt\ttestdata/scripts/script_hello.ps1'
    $'default_block\t.\ttestdata/scripts/script_block.ps1'
    $'alt_block\t-alt\ttestdata/scripts/script_block.ps1'
)

COVER_REL="testdata/covers/cover.jpg"
COVER="$TESTS/$COVER_REL"
if [[ ! -f "$COVER" ]]; then
    echo "Missing cover fixture: $COVER" >&2
    exit 1
fi

printf '%s\t%s\t%s\t%s\n' case_id option script_rel golden_rel > "$STAGED_MANIFEST"

for row in "${CASES[@]}"; do
    IFS=$'\t' read -r case_id option script_rel <<<"$row"
    script="$TESTS/$script_rel"
    case_work="$STAGE_ROOT/work/$case_id"
    run_log="$case_work/run.log"

    if [[ ! -f "$script" ]]; then
        echo "Missing script fixture for $case_id: $script" >&2
        exit 1
    fi

    mkdir -p "$case_work" "$STAGED_GOLDEN/$case_id"
    cp "$COVER" "$case_work/cover.jpg"
    cp "$script" "$case_work/script.ps1"

    if [[ "$option" == "-alt" ]]; then
        if ! (cd "$case_work" && "$BIN" -alt cover.jpg script.ps1) >"$run_log" 2>&1; then
            echo "jpws failed for $case_id:" >&2
            sed 's/^/    /' "$run_log" >&2
            exit 1
        fi
    else
        if ! (cd "$case_work" && "$BIN" cover.jpg script.ps1) >"$run_log" 2>&1; then
            echo "jpws failed for $case_id:" >&2
            sed 's/^/    /' "$run_log" >&2
            exit 1
        fi
    fi

    output="$(jpws_extract_output_image "$run_log")"
    if [[ -z "$output" || "$output" != "$(basename -- "$output")" || ! -f "$case_work/$output" ]]; then
        echo "jpws did not report a valid output file for $case_id:" >&2
        sed 's/^/    /' "$run_log" >&2
        exit 1
    fi

    golden_rel="golden/$case_id/polyglot.jpg"
    staged_poly="$STAGED_GOLDEN/$case_id/polyglot.jpg"
    cp "$case_work/$output" "$staged_poly"

    if ! jpws_assert_polyglot_structure "$staged_poly" "$script" "$option"; then
        echo "Generated polyglot failed structure validation for $case_id" >&2
        exit 1
    fi
    if ! jpws_assert_jpeg_decodes "$staged_poly"; then
        echo "Generated polyglot failed JPEG decode validation for $case_id" >&2
        exit 1
    fi

    printf '%s\t%s\t%s\t%s\n' "$case_id" "$option" "$script_rel" "$golden_rel" >> "$STAGED_MANIFEST"
done

jpws_validate_manifest "$STAGED_MANIFEST"

if [[ -e "$GOLDEN" || -L "$GOLDEN" ]]; then
    if [[ ! -d "$GOLDEN" || -L "$GOLDEN" ]]; then
        echo "Refusing to replace non-directory golden path: $GOLDEN" >&2
        exit 1
    fi
    mv "$GOLDEN" "$BACKUP_GOLDEN"
    HAD_GOLDEN=1
fi

# This rename is on the same filesystem as GOLDEN. If it fails, the EXIT trap
# restores the previous directory before removing the staging area.
if ! mv "$STAGED_GOLDEN" "$GOLDEN"; then
    echo "Unable to install staged golden fixtures" >&2
    exit 1
fi

echo "Golden fixtures written to: $GOLDEN"
echo "Manifest: $GOLDEN/manifest.tsv"
wc -l "$GOLDEN/manifest.tsv"
