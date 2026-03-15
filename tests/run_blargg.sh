#!/usr/bin/env bash
# tests/run_blargg.sh - Blargg ROM test runner for SeaBoy.
#
# Usage:
#   bash tests/run_blargg.sh [options]
#
# Options:
#   --build-dir <path>   Build directory (default: build/Release)
#   --no-build           Skip cmake --build step
#   -h, --help           Show this help
#
# Exit code: 0 if all tests pass, 1 if any fail.

set -uo pipefail

# ---------------------------------------------------------------------------
# ANSI colors
# ---------------------------------------------------------------------------
C_RESET='\033[0m'
C_GREEN='\033[0;32m'
C_RED='\033[0;31m'
C_CYAN='\033[0;36m'
C_BOLD='\033[1m'

color_status() {
    local s="$1"
    case "$s" in
        PASS) printf "${C_GREEN}PASS${C_RESET}" ;;
        FAIL) printf "${C_RED}FAIL${C_RESET}"   ;;
        SKIP) printf "${C_CYAN}SKIP${C_RESET}"  ;;
        *)    printf "%s" "$s"                  ;;
    esac
}

# ---------------------------------------------------------------------------
# Windows detection (.exe suffix)
# ---------------------------------------------------------------------------
EXE_SUFFIX=""
_uname="$(uname -s 2>/dev/null || true)"
if [[ "$OSTYPE" == "msys"* ]] || [[ "$OSTYPE" == "cygwin"* ]] || \
   [[ "$_uname" == MINGW* ]] || [[ "$_uname" == CYGWIN* ]]; then
    EXE_SUFFIX=".exe"
fi

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
BUILD_DIR="build/Release"
DO_BUILD=1

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --no-build)   DO_BUILD=0;     shift   ;;
        -h|--help)    sed -n '3,15p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Paths — always run from project root so sub-scripts find roms/ etc.
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

# Resolve BUILD_DIR relative to root if not absolute
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
resolve_binary() {
    # Sets RESOLVED_BIN; returns 0 if found, 1 if not.
    local bin="$BUILD_DIR/$1"
    if [[ -x "${bin}${EXE_SUFFIX}" ]]; then RESOLVED_BIN="${bin}${EXE_SUFFIX}"; return 0; fi
    if [[ -x "$bin" ]]; then RESOLVED_BIN="$bin"; return 0; fi
    RESOLVED_BIN=""; return 1
}

SEP="================================================================="
DASH="-----------------------------------------------------------------"

# ===========================================================================
# HEADER
# ===========================================================================
echo ""
echo -e "${C_BOLD}=== SeaBoy Blargg Test Runner ===${C_RESET}"
echo ""

# ===========================================================================
# STEP 0: BUILD
# ===========================================================================
if [[ $DO_BUILD -eq 1 ]]; then
    echo -e "${C_BOLD}--- Build ---${C_RESET}"
    if cmake --build "$BUILD_DIR"; then
        BUILD_STATUS="PASS"
        echo ""
        echo -e "Build: $(color_status PASS)"
    else
        BUILD_STATUS="FAIL"
        echo ""
        echo -e "Build: $(color_status FAIL) — aborting (fix build errors first)"
        echo ""
        exit 1
    fi
else
    echo "Build skipped (--no-build)."
    BUILD_STATUS="SKIP"
fi
echo ""

# ===========================================================================
# STEP 1: BLARGG
# ===========================================================================
echo -e "${C_BOLD}--- Blargg ROM Tests ---${C_RESET}"

BLARGG_ROMS=(
    "roms/blargg/cpu_instrs.gb"
    "roms/blargg/instr_timing.gb"
    "roms/blargg/interrupt_time.gb"
    "roms/blargg/cgb_sound/cgb_sound.gb"
    "roms/blargg/cgb_sound/rom_singles/01-registers.gb"
    "roms/blargg/cgb_sound/rom_singles/02-len ctr.gb"
    "roms/blargg/cgb_sound/rom_singles/03-trigger.gb"
    "roms/blargg/cgb_sound/rom_singles/04-sweep.gb"
    "roms/blargg/cgb_sound/rom_singles/05-sweep details.gb"
    "roms/blargg/cgb_sound/rom_singles/06-overflow on trigger.gb"
    "roms/blargg/cgb_sound/rom_singles/07-len sweep period sync.gb"
    "roms/blargg/cgb_sound/rom_singles/08-len ctr during power.gb"
    "roms/blargg/cgb_sound/rom_singles/09-wave read while on.gb"
    "roms/blargg/cgb_sound/rom_singles/10-wave trigger while on.gb"
    "roms/blargg/cgb_sound/rom_singles/11-regs after power.gb"
    "roms/blargg/cgb_sound/rom_singles/12-wave.gb"
    "roms/blargg/dmg_sound/dmg_sound.gb"
    "roms/blargg/dmg_sound/rom_singles/01-registers.gb"
    "roms/blargg/dmg_sound/rom_singles/02-len ctr.gb"
    "roms/blargg/dmg_sound/rom_singles/03-trigger.gb"
    "roms/blargg/dmg_sound/rom_singles/04-sweep.gb"
    "roms/blargg/dmg_sound/rom_singles/05-sweep details.gb"
    "roms/blargg/dmg_sound/rom_singles/06-overflow on trigger.gb"
    "roms/blargg/dmg_sound/rom_singles/07-len sweep period sync.gb"
    "roms/blargg/dmg_sound/rom_singles/08-len ctr during power.gb"
    "roms/blargg/dmg_sound/rom_singles/09-wave read while on.gb"
    "roms/blargg/dmg_sound/rom_singles/10-wave trigger while on.gb"
    "roms/blargg/dmg_sound/rom_singles/11-regs after power.gb"
    "roms/blargg/dmg_sound/rom_singles/12-wave write while on.gb"
    "roms/blargg/oam_bug/1-lcd_sync.gb"
    "roms/blargg/oam_bug/2-causes.gb"
    "roms/blargg/oam_bug/3-non_causes.gb"
    "roms/blargg/oam_bug/4-scanline_timing.gb"
    "roms/blargg/oam_bug/5-timing_bug.gb"
    "roms/blargg/oam_bug/6-timing_no_bug.gb"
    "roms/blargg/oam_bug/7-timing_effect.gb"
    "roms/blargg/oam_bug/8-instr_effect.gb"
    "roms/blargg/oam_bug/oam_bug.gb"
    "roms/blargg/mem_timing/mem_timing.gb"
    "roms/blargg/mem_timing-2/mem_timing.gb"
    "roms/blargg/halt_bug.gb"
)

if ! resolve_binary "blargg_runner"; then
    echo "ERROR: blargg_runner not found in $BUILD_DIR"
    exit 1
fi

BLARGG_BIN="$RESOLVED_BIN"
blargg_pass=0
blargg_fail=0
blargg_skip=0

declare -a ROM_LABELS
declare -a ROM_STATUS

for rom_rel in "${BLARGG_ROMS[@]}"; do
    rom="$ROOT_DIR/$rom_rel"
    # Label: parent-dir/filename for roms in subdirs, just filename for top-level
    parent="$(basename "$(dirname "$rom_rel")")"
    fname="$(basename "$rom_rel")"
    if [[ "$parent" == "blargg" ]]; then
        label="$fname"
    else
        label="$parent/$fname"
    fi
    ROM_LABELS+=("$label")

    if [[ ! -f "$rom" ]]; then
        printf "  SKIP  %s  (not found)\n" "$label"
        ROM_STATUS+=("SKIP")
        ((blargg_skip++)) || true
        continue
    fi

    printf "  %-42s  " "$label"
    _tmpfile="/tmp/blargg_$$_${blargg_pass}${blargg_fail}.txt"
    if "$BLARGG_BIN" "$rom" > "$_tmpfile" 2>&1; then
        echo -e "$(color_status PASS)"
        ((blargg_pass++)) || true
        ROM_STATUS+=("PASS")
    else
        echo -e "$(color_status FAIL)"
        tail -6 "$_tmpfile" | sed 's/^/    /'
        ((blargg_fail++)) || true
        ROM_STATUS+=("FAIL")
    fi
    rm -f "$_tmpfile"
done

echo ""

# ===========================================================================
# SUMMARY TABLE
# ===========================================================================
echo -e "${C_BOLD}$SEP${C_RESET}"
printf "${C_BOLD}%s${C_RESET}\n" "                    TEST SUMMARY"
echo -e "${C_BOLD}$SEP${C_RESET}"
printf "${C_BOLD}%-42s  %s${C_RESET}\n" "Test" "Status"
echo "$DASH"

for i in "${!ROM_LABELS[@]}"; do
    printf "%-42s  %b\n" "${ROM_LABELS[$i]}" "$(color_status "${ROM_STATUS[$i]}")"
done

echo "$DASH"

total_run=$(( blargg_pass + blargg_fail ))
total_tests=${#ROM_LABELS[@]}

printf "${C_BOLD}%-42s  %s${C_RESET}\n" \
    "TOTAL" "$(color_status $([ $blargg_fail -eq 0 ] && echo PASS || echo FAIL))"
printf "Passed: %d  Failed: %d  Skipped: %d  Total: %d\n" \
    $blargg_pass $blargg_fail $blargg_skip $total_tests
echo -e "${C_BOLD}$SEP${C_RESET}"
echo ""

# ===========================================================================
# EXIT
# ===========================================================================
if [[ $blargg_fail -eq 0 ]]; then
    echo -e "${C_GREEN}All tests passed!${C_RESET}"
    exit 0
else
    echo -e "${C_RED}Some tests failed.${C_RESET}"
    exit 1
fi
