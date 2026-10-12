#!/bin/bash
# Create deterministic cover JPEGs and PowerShell scripts for jpws golden tests.
#
# Dependencies: ffmpeg (synthesizes the cover JPEG; no committed binary fixture)
# and a POSIX shell. Scripts are plain UTF-8 (no BOM) so they embed verbatim.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="$TESTS/testdata"
mkdir -p "$DATA/covers" "$DATA/scripts"

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd ffmpeg

# --- Cover JPEG ------------------------------------------------------------
# 512x512 (>= the 400px floor), high quality. testsrc2 is a busy gradient/noise
# pattern, so it rarely needs the "#>"-removal recompression ladder — the golden
# generation exercises whichever path the encoder lands on.
COVER="$DATA/covers/cover.jpg"
if [[ ! -f "$COVER" ]]; then
    ffmpeg -hide_banner -loglevel error -y \
        -f lavfi -i "testsrc2=size=512x512:rate=1" -frames:v 1 -q:v 3 "$COVER"
fi

# --- PowerShell scripts (plain UTF-8, >= 10 bytes) -------------------------
HELLO="$DATA/scripts/script_hello.ps1"
if [[ ! -f "$HELLO" ]]; then
    printf 'Write-Host "hello from the jpws polyglot golden test"\nGet-Date\n' > "$HELLO"
fi

BLOCK="$DATA/scripts/script_block.ps1"
if [[ ! -f "$BLOCK" ]]; then
    cat > "$BLOCK" <<'PS1'
# jpws golden-test script (block)
$items = 1..5 | ForEach-Object { "item-$_" }
foreach ($i in $items) {
    Write-Output $i
}
Write-Host "done"
PS1
fi

echo "Testdata ready under: $DATA"
