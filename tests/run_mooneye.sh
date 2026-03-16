#!/usr/bin/env bash
# run_mooneye.sh - Batch runner for mooneye-test-suite DMG-compatible tests.
#
# Usage:
#   bash tests/run_mooneye.sh [build-dir]
#
# Defaults to build/Release. Pass an alternate build dir as the first argument,
# e.g.:  bash tests/run_mooneye.sh build/Debug
#
# Runs acceptance/, emulator-only/, and misc/cgb/ tests.
# Skips tests targeting non-CGB, non-DMG hardware (sgb/mgb/agb/ags in filename).
# Skips manual-only/, misc/(non-CGB), and madness/ directories.
#
# Requires:
#   - mooneye_runner binary (build it first: cmake --build build --target mooneye_runner)
#   - ROMs cloned to roms/mooneye-test-suite/
#     (git clone https://github.com/Gekkio/mooneye-test-suite roms/mooneye-test-suite)

set -euo pipefail

BUILD_DIR="${1:-build/Release}"
RUNNER="$BUILD_DIR/mooneye_runner"
ROMS_DIR="roms/mooneye-test-suite"

if [ ! -x "$RUNNER" ] && [ ! -x "${RUNNER}.exe" ]; then
    echo "ERROR: runner not found at $RUNNER - run: cmake --build build --target mooneye_runner"
    exit 1
fi

if [ -x "${RUNNER}.exe" ]; then
    RUNNER="${RUNNER}.exe"
fi

if [ ! -d "$ROMS_DIR" ]; then
    echo "ERROR: ROM directory not found: $ROMS_DIR"
    echo "Clone with: git clone https://github.com/Gekkio/mooneye-test-suite $ROMS_DIR"
    exit 1
fi

pass=0
fail=0
skip=0

echo "Running mooneye DMG-compatible tests..."
echo "Runner:  $RUNNER"
echo "ROMs:    $ROMS_DIR"
echo "---"

# Run acceptance/, emulator-only/, and misc/cgb/ - skip manual-only/, misc/(other), and madness/
for dir in acceptance emulator-only misc/cgb; do
    subdir="$ROMS_DIR/$dir"
    [ -d "$subdir" ] || continue

    # Find all .gb files recursively in this directory
    while IFS= read -r -d '' rom; do
        name=$(basename "$rom")

        # Skip tests targeting non-DMG, non-CGB hardware based on filename patterns
        if echo "$name" | grep -qiE 'sgb|mgb|agb|ags|-[SA]\.gb$'; then
            rel="${rom#$ROMS_DIR/}"; rel="${rel%.gb}"
            echo "SKIP  $rel"
            ((skip++)) || true
            continue
        fi

        rel="${rom#$ROMS_DIR/}"; rel="${rel%.gb}"
        if "$RUNNER" "$rom" > /dev/null 2>&1; then
            echo "PASS  $rel"
            ((pass++)) || true
        else
            echo "FAIL  $rel"
            ((fail++)) || true
        fi
    done < <(find "$subdir" -name '*.gb' -print0 | sort -z)
done

echo "---"
echo "Results: $pass passed, $fail failed, $skip skipped"

[ "$fail" -eq 0 ]
