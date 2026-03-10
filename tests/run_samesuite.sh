#!/usr/bin/env bash
# run_samesuite.sh - Batch runner for SameSuite DMG-compatible tests.
#
# Usage:
#   bash tests/run_samesuite.sh [build-dir]
#
# Defaults to build/Release. Pass an alternate build dir as the first argument,
# e.g.:  bash tests/run_samesuite.sh build/Debug
#
# Reuses mooneye_runner (same Fibonacci register pass/fail protocol).
# Skips tests targeting CGB-only or SGB-only hardware:
#   - Files with "cgb" in filename (CGB revision-specific)
#   - dma/ directory (HDMA/GDMA - GBC only)
#   - sgb/ directory (SGB only)
#   - ppu/blocking_bgpi_increase.gb (CGB palette register BGPI)
#
# Requires:
#   - mooneye_runner binary (same protocol: Fibonacci regs + LD B,B breakpoint)
#   - ROMs built in roms/SameSuite-master/
#     (cd roms/SameSuite-master && make)

set -euo pipefail

BUILD_DIR="${1:-build/Release}"
RUNNER="$BUILD_DIR/mooneye_runner"
ROMS_DIR="roms/SameSuite-master"

if [ ! -x "$RUNNER" ] && [ ! -x "${RUNNER}.exe" ]; then
    echo "ERROR: mooneye_runner not found at $RUNNER - run: cmake --build build --target mooneye_runner"
    exit 1
fi

if [ -x "${RUNNER}.exe" ]; then
    RUNNER="${RUNNER}.exe"
fi

if [ ! -d "$ROMS_DIR" ]; then
    echo "ERROR: ROM directory not found: $ROMS_DIR"
    echo "Build with: cd roms/SameSuite-master && make"
    exit 1
fi

pass=0
fail=0
skip=0

echo "Running SameSuite DMG-compatible tests..."
echo "Runner:  $RUNNER"
echo "ROMs:    $ROMS_DIR"
echo "---"

while IFS= read -r -d '' rom; do
    # Get path relative to ROMS_DIR for display
    relpath="${rom#$ROMS_DIR/}"

    # Skip CGB revision-specific tests (filename contains "cgb")
    if echo "$relpath" | grep -qi 'cgb'; then
        echo "SKIP  $relpath  (CGB-specific)"
        ((skip++)) || true
        continue
    fi

    # Skip dma/ directory (all GBC-only: HDMA, GDMA)
    if echo "$relpath" | grep -q '^dma/'; then
        echo "SKIP  $relpath  (GBC DMA)"
        ((skip++)) || true
        continue
    fi

    # Skip sgb/ directory (SGB-only)
    if echo "$relpath" | grep -q '^sgb/'; then
        echo "SKIP  $relpath  (SGB-only)"
        ((skip++)) || true
        continue
    fi

    # Skip CGB-only PPU test
    if echo "$relpath" | grep -q '^ppu/blocking_bgpi_increase'; then
        echo "SKIP  $relpath  (CGB palette)"
        ((skip++)) || true
        continue
    fi

    if "$RUNNER" "$rom" --timeout 120 2>/dev/null; then
        ((pass++)) || true
    else
        ((fail++)) || true
    fi
done < <(find "$ROMS_DIR" -name '*.gb' -print0 | sort -z)

echo "---"
echo "Results: $pass passed, $fail failed, $skip skipped"

[ "$fail" -eq 0 ]
