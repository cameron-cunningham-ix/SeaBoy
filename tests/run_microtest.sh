#!/usr/bin/env bash
# run_microtest.sh - Batch runner for gbmicrotest ROM suite.
#
# Usage:
#   bash tests/run_microtest.sh [build-dir]
#
# Defaults to build/Release. Pass an alternate build dir as the first argument.
#
# Requires:
#   - microtest_runner binary (cmake --build build --target microtest_runner)
#   - ROMs in roms/gbmicrotest-main/bin/

set -euo pipefail

BUILD_DIR="${1:-build/Release}"
RUNNER="$BUILD_DIR/microtest_runner"
ROMS_DIR="roms/gbmicrotest-main/bin"

if [ ! -x "$RUNNER" ] && [ ! -x "${RUNNER}.exe" ]; then
    echo "ERROR: runner not found at $RUNNER - run: cmake --build build --target microtest_runner"
    exit 1
fi

if [ -x "${RUNNER}.exe" ]; then
    RUNNER="${RUNNER}.exe"
fi

if [ ! -d "$ROMS_DIR" ]; then
    echo "ERROR: ROM directory not found: $ROMS_DIR"
    exit 1
fi

# Files to skip: testbenches, temp files, and tests that don't use the standard
# 0xFF80-0xFF82 pass/fail protocol (they write raw values to VRAM instead).
SKIP_PATTERNS=(
    # Testbenches and temp files
    "temp.gb" "audio_testbench.gb" "ppu_sprite_testbench.gb"
    # Non-standard tests (no test_finish macro - write to VRAM directly)
    "000-oam_lock.gb" "000-write_to_x8000.gb" "001-vram_unlocked.gb"
    "002-vram_locked.gb" "004-tima_boot_phase.gb" "004-tima_cycle_timer.gb"
    "007-lcd_on_stat.gb" "400-dma.gb" "500-scx-timing.gb"
    "800-ppu-latch-scx.gb" "801-ppu-latch-scy.gb"
    "802-ppu-latch-tileselect.gb" "803-ppu-latch-bgdisplay.gb"
    "cpu_bus_1.gb" "dma_basic.gb" "flood_vram.gb"
    "hblank_int_di_timing_a.gb" "hblank_int_di_timing_b.gb"
    "hblank_int_scx3.gb"
    "hblank_scx3_if_a.gb" "hblank_scx3_int_a.gb" "hblank_scx3_int_b.gb"
    "lcdon_write_timing.gb"
    "line_153_lyc_int_a.gb" "line_153_lyc_int_b.gb"
    "ly_while_lcd_off.gb"
    "lyc1_write_timing_a.gb" "lyc1_write_timing_b.gb"
    "lyc1_write_timing_c.gb" "lyc1_write_timing_d.gb"
    "mbc1_ram_banks.gb" "mbc1_rom_banks.gb" "minimal.gb"
    "mode2_stat_int_to_oam_unlock.gb" "oam_sprite_trashing.gb"
    "poweron.gb" "ppu_scx_vs_bgp.gb" "ppu_spritex_vs_scx.gb"
    "ppu_win_vs_wx.gb" "ppu_wx_early.gb" "toggle_lcdc.gb"
    "wave_write_to_0xC003.gb"
)

pass=0
fail=0
skip=0

echo "Running gbmicrotest suite..."
echo "Runner:  $RUNNER"
echo "ROMs:    $ROMS_DIR"
echo "---"

for rom in "$ROMS_DIR"/*.gb; do
    name=$(basename "$rom")

    # Skip non-standard tests
    skip_this=0
    for s in "${SKIP_PATTERNS[@]}"; do
        if [ "$name" = "$s" ]; then
            ((skip++)) || true
            skip_this=1
            break
        fi
    done
    [ "$skip_this" -eq 1 ] && continue

    if "$RUNNER" "$rom" 2>/dev/null; then
        ((pass++)) || true
    else
        ((fail++)) || true
    fi
done

echo "---"
echo "Results: $pass passed, $fail failed, $skip skipped"

[ "$fail" -eq 0 ]
