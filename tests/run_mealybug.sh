#!/usr/bin/env bash
# run_mealybug.sh - Batch runner for all DMG mealybug-tearoom PPU tests.
#
# Usage:
#   bash tests/run_mealybug.sh [build-dir]
#
# Defaults to build/Release. Pass an alternate build dir as the first argument,
# e.g.:  bash tests/run_mealybug.sh build/Debug
#
# Requires:
#   - mealybug_runner binary (build it first: cmake --build build --target mealybug_runner)
#   - ROMs cloned to roms/mealybug-tearoom-tests/
#     (git clone https://github.com/mattcurrie/mealybug-tearoom-tests roms/mealybug-tearoom-tests)
#   - ImageMagick (magick) on PATH

set -euo pipefail

BUILD_DIR="${1:-build/Release}"
RUNNER="$BUILD_DIR/mealybug_runner"
ROMS_DIR="roms/mealybug-tearoom-tests/ppu"

if [ ! -x "$RUNNER" ] && [ ! -x "${RUNNER}.exe" ]; then
    echo "ERROR: runner not found at $RUNNER - run: cmake --build build --target mealybug_runner"
    exit 1
fi

# On Windows (Git Bash) the binary has .exe suffix
if [ -x "${RUNNER}.exe" ]; then
    RUNNER="${RUNNER}.exe"
fi

if [ ! -d "$ROMS_DIR" ]; then
    echo "ERROR: ROM directory not found: $ROMS_DIR"
    echo "Clone with: git clone https://github.com/mattcurrie/mealybug-tearoom-tests roms/mealybug-tearoom-tests"
    exit 1
fi

pass=0
fail=0
skip=0

echo "Running mealybug DMG PPU tests..."
echo "Runner:  $RUNNER"
echo "ROMs:    $ROMS_DIR"
echo "---"

for rom in "$ROMS_DIR"/*.gb; do
    expected="${rom%.gb}.dmg.png"
    if [ ! -f "$expected" ]; then
        echo "SKIP  $(basename "$rom")  (no .dmg.png)"
        ((skip++)) || true
        continue
    fi
    if "$RUNNER" "$rom" "$expected"; then
        ((pass++)) || true
    else
        ((fail++)) || true
    fi
done

echo "---"
echo "Results: $pass passed, $fail failed, $skip skipped"

# Exit non-zero if any test failed
[ "$fail" -eq 0 ]
