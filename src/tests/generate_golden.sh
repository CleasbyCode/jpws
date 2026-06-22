#!/bin/bash
# Build jpws and regenerate the committed golden polyglot fixtures + manifest.
#
# jpws is deterministic apart from the random output filename, so each golden is
# a real built polyglot. Re-run after an intentional format change, then review
# the diffs to golden/*/polyglot.jpg and golden/manifest.tsv before committing.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
GOLDEN="$TESTS/golden"
MANIFEST="$GOLDEN/manifest.tsv"
BIN="${JPWS_BIN:-$ROOT/jpws}"
NO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1; shift;;
        *) echo "Unknown option: $1" >&2; exit 2;;
    esac
done

bash "$TESTS/create_testdata.sh"

if [[ "$NO_BUILD" -eq 0 ]]; then
    (cd "$ROOT" && env -u JPWS_NATIVE bash ./compile_jpws.sh)
fi
if [[ ! -x "$BIN" ]]; then
    echo "Binary not found: $BIN" >&2
    exit 1
fi

extract_output_image() {
    sed -n 's/.*polyglot image: \([^ ]*\) (.*/\1/p' "$1" | tail -n 1
}

# case_id  option(.|-alt)  script_rel
CASES=(
    $'default_hello\t.\ttestdata/scripts/script_hello.ps1'
    $'alt_hello\t-alt\ttestdata/scripts/script_hello.ps1'
    $'default_block\t.\ttestdata/scripts/script_block.ps1'
    $'alt_block\t-alt\ttestdata/scripts/script_block.ps1'
)

COVER_REL="testdata/covers/cover.jpg"

rm -rf "$GOLDEN"
mkdir -p "$GOLDEN"

{
    printf '%s\t%s\t%s\t%s\n' case_id option script_rel golden_rel
    for row in "${CASES[@]}"; do
        IFS=$'\t' read -r case_id option script_rel <<<"$row"
        opt="$option"
        [[ "$opt" == "." ]] && opt=""

        cover="$TESTS/$COVER_REL"
        script="$TESTS/$script_rel"
        if [[ ! -f "$cover" || ! -f "$script" ]]; then
            echo "Missing fixture for $case_id" >&2
            exit 1
        fi

        mkdir -p "$GOLDEN/$case_id"
        work="$(mktemp -d)"
        pushd "$work" >/dev/null
        cp "$cover" cover.jpg
        cp "$script" script.ps1
        if [[ -n "$opt" ]]; then
            "$BIN" "$opt" cover.jpg script.ps1 > run.log 2>&1
        else
            "$BIN" cover.jpg script.ps1 > run.log 2>&1
        fi
        output="$(extract_output_image run.log)"
        if [[ -z "$output" || ! -f "$output" ]]; then
            echo "jpws failed for $case_id:" >&2
            cat run.log >&2
            popd >/dev/null
            rm -rf "$work"
            exit 1
        fi
        popd >/dev/null

        golden_rel="golden/$case_id/polyglot.jpg"
        cp "$work/$output" "$TESTS/$golden_rel"
        rm -rf "$work"

        printf '%s\t%s\t%s\t%s\n' "$case_id" "$option" "$script_rel" "$golden_rel"
    done
} > "$MANIFEST"

echo "Golden fixtures written to: $GOLDEN"
echo "Manifest: $MANIFEST"
wc -l "$MANIFEST"
