#!/usr/bin/env bash
# tests/run_all.sh - Unified regression test runner for SeaBoy.
#
# Usage:
#   bash tests/run_all.sh [options]
#
# Options:
#   --build-dir <path>   Build directory (default: build/Release)
#   --no-build           Skip cmake --build step
#   --sm83               Run SM83 single-step tests (off by default; slow)
#   --no-blargg          Skip Blargg ROM tests
#   --no-mealybug        Skip Mealybug PPU visual tests
#   --no-mooneye         Skip Mooneye hardware tests
#   --no-microtest       Skip gbmicrotest cycle-accurate tests
#   --no-samesuite       Skip SameSuite APU/DMA/PPU tests
#
# Exit code: 0 if all enabled suites pass, 1 if any fail.

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
RUN_SM83=0
RUN_BLARGG=1
RUN_MEALYBUG=1
RUN_MOONEYE=1
RUN_MICROTEST=1
RUN_SAMESUITE=1

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --no-build)   DO_BUILD=0;     shift   ;;
        --sm83)       RUN_SM83=1;     shift   ;;
        --no-blargg)  RUN_BLARGG=0;   shift   ;;
        --no-mealybug) RUN_MEALYBUG=0; shift  ;;
        --no-mooneye) RUN_MOONEYE=0;  shift   ;;
        --no-microtest) RUN_MICROTEST=0; shift ;;
        --no-samesuite) RUN_SAMESUITE=0; shift ;;
        -h|--help)    sed -n '2,16p' "$0"; exit 0 ;;
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
# Summary state
# ---------------------------------------------------------------------------
# Indices: 0=Build 1=Blargg 2=SM83 3=Mealybug 4=Mooneye 5=Microtest 6=SameSuite
SUITE_NAMES=("Build" "Blargg" "SM83" "Mealybug" "Mooneye" "Microtest" "SameSuite")
SUITE_STATUS=("SKIP" "SKIP" "SKIP" "SKIP" "SKIP" "SKIP" "SKIP")
SUITE_PASS=(0 0 0 0 0 0 0)
SUITE_FAIL=(0 0 0 0 0 0 0)
SUITE_NOTES=("" "" "use --sm83 to enable" "" "" "" "")

BLARGG_ROM_LABELS=()
BLARGG_ROM_STATUS=()

OVERALL_FAIL=0

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

parse_results() {
    # Parses "Results: N passed, N failed, N skipped" from $1.
    # Sets PARSED_PASS / PARSED_FAIL; returns 1 if not found.
    local line
    line=$(printf '%s' "$1" | grep -E '^Results:' | tail -1)
    [[ -z "$line" ]] && { PARSED_PASS=0; PARSED_FAIL=0; return 1; }
    PARSED_PASS=$(printf '%s' "$line" | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+' || echo 0)
    PARSED_FAIL=$(printf '%s' "$line" | grep -oE '[0-9]+ failed' | grep -oE '[0-9]+' || echo 0)
    return 0
}

SEP="================================================================="
DASH="-----------------------------------------------------------------"

# ===========================================================================
# HEADER
# ===========================================================================
echo ""
echo -e "${C_BOLD}=== SeaBoy Regression Test Runner ===${C_RESET}"
echo ""

# ===========================================================================
# STEP 0: BUILD
# ===========================================================================
if [[ $DO_BUILD -eq 1 ]]; then
    echo -e "${C_BOLD}--- Build ---${C_RESET}"
    if cmake --build "$BUILD_DIR"; then
        SUITE_STATUS[0]="PASS"
        echo ""
        echo -e "Build: $(color_status PASS)"
    else
        SUITE_STATUS[0]="FAIL"
        OVERALL_FAIL=1
        echo ""
        echo -e "Build: $(color_status FAIL) — aborting (fix build errors first)"
        echo ""
        # Print minimal summary then bail
        echo -e "${C_BOLD}$SEP${C_RESET}"
        echo -e "${C_BOLD}                    TEST SUITE SUMMARY${C_RESET}"
        echo -e "${C_BOLD}$SEP${C_RESET}"
        printf "${C_BOLD}%-16s  %-4s${C_RESET}\n" "Suite" "Status"
        echo "$DASH"
        printf "%-16s  %b\n" "Build" "$(color_status FAIL)"
        echo -e "${C_BOLD}$SEP${C_RESET}"
        exit 1
    fi
else
    SUITE_STATUS[0]="SKIP"
    SUITE_NOTES[0]="--no-build"
    echo "Build skipped (--no-build)."
fi
echo ""

# ===========================================================================
# STEP 1: BLARGG
# ===========================================================================
if [[ $RUN_BLARGG -eq 1 ]]; then
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
        echo "WARNING: blargg_runner not found in $BUILD_DIR"
        SUITE_STATUS[1]="SKIP"
        SUITE_NOTES[1]="binary missing"
    else
        BLARGG_BIN="$RESOLVED_BIN"
        blargg_pass=0
        blargg_fail=0

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
            BLARGG_ROM_LABELS+=("$label")

            if [[ ! -f "$rom" ]]; then
                printf "  SKIP  %s  (not found)\n" "$label"
                BLARGG_ROM_STATUS+=("SKIP")
                continue
            fi

            printf "  %-42s  " "$label"
            _tmpfile="/tmp/blargg_$$_${blargg_pass}${blargg_fail}.txt"
            if "$BLARGG_BIN" "$rom" > "$_tmpfile" 2>&1; then
                echo -e "$(color_status PASS)"
                ((blargg_pass++)) || true
                BLARGG_ROM_STATUS+=("PASS")
            else
                echo -e "$(color_status FAIL)"
                tail -6 "$_tmpfile" | sed 's/^/    /'
                ((blargg_fail++)) || true
                BLARGG_ROM_STATUS+=("FAIL")
            fi
            rm -f "$_tmpfile"
        done

        SUITE_PASS[1]=$blargg_pass
        SUITE_FAIL[1]=$blargg_fail
        if [[ $blargg_fail -eq 0 ]]; then
            SUITE_STATUS[1]="PASS"
        else
            SUITE_STATUS[1]="FAIL"
            OVERALL_FAIL=1
        fi
    fi
    echo ""
fi

# ===========================================================================
# STEP 2: SM83
# ===========================================================================
if [[ $RUN_SM83 -eq 1 ]]; then
    echo -e "${C_BOLD}--- SM83 Single-Step Tests (500,000 cases) ---${C_RESET}"
    SM83_DATA="$ROOT_DIR/tests/sm83_data/v1"

    if ! resolve_binary "sm83_runner"; then
        echo "WARNING: sm83_runner not found in $BUILD_DIR"
        SUITE_STATUS[2]="SKIP"
        SUITE_NOTES[2]="binary missing"
    elif [[ ! -d "$SM83_DATA" ]]; then
        echo "WARNING: SM83 test data not found at tests/sm83_data/v1"
        echo "         git clone --depth 1 https://github.com/SingleStepTests/sm83.git tests/sm83_data"
        SUITE_STATUS[2]="SKIP"
        SUITE_NOTES[2]="data missing"
    else
        SM83_BIN="$RESOLVED_BIN"
        echo "Runner: $SM83_BIN"
        echo "(This may take several minutes...)"
        if "$SM83_BIN" "$SM83_DATA"; then
            SUITE_STATUS[2]="PASS"
            SUITE_PASS[2]=500000
        else
            SUITE_STATUS[2]="FAIL"
            SUITE_FAIL[2]=1
            OVERALL_FAIL=1
        fi
    fi
    echo ""
else
    SUITE_STATUS[2]="SKIP"
    # Note already set in defaults
fi

# ===========================================================================
# STEP 3: MEALYBUG
# ===========================================================================
if [[ $RUN_MEALYBUG -eq 1 ]]; then
    echo -e "${C_BOLD}--- Mealybug PPU Visual Tests ---${C_RESET}"
    MEALYBUG_SCRIPT="$SCRIPT_DIR/run_mealybug.sh"

    if [[ ! -f "$MEALYBUG_SCRIPT" ]]; then
        echo "ERROR: run_mealybug.sh not found"
        SUITE_STATUS[3]="FAIL"
        OVERALL_FAIL=1
    else
        set +e
        mb_output=$(bash "$MEALYBUG_SCRIPT" "$BUILD_DIR" 2>&1)
        mb_exit=$?
        set -e

        printf '%s\n' "$mb_output" | sed 's/^/  /'
        echo ""

        if parse_results "$mb_output"; then
            SUITE_PASS[3]=$PARSED_PASS
            SUITE_FAIL[3]=$PARSED_FAIL
        else
            SUITE_NOTES[3]="no results line"
        fi

        if [[ $mb_exit -eq 0 ]]; then
            SUITE_STATUS[3]="PASS"
        else
            SUITE_STATUS[3]="FAIL"
            OVERALL_FAIL=1
        fi
    fi
    echo ""
fi

# ===========================================================================
# STEP 4: MOONEYE
# ===========================================================================
if [[ $RUN_MOONEYE -eq 1 ]]; then
    echo -e "${C_BOLD}--- Mooneye Hardware Tests ---${C_RESET}"
    MOONEYE_SCRIPT="$SCRIPT_DIR/run_mooneye.sh"

    if [[ ! -f "$MOONEYE_SCRIPT" ]]; then
        echo "ERROR: run_mooneye.sh not found"
        SUITE_STATUS[4]="FAIL"
        OVERALL_FAIL=1
    else
        set +e
        mn_output=$(bash "$MOONEYE_SCRIPT" "$BUILD_DIR" 2>&1)
        mn_exit=$?
        set -e

        printf '%s\n' "$mn_output" | sed 's/^/  /'
        echo ""

        if parse_results "$mn_output"; then
            SUITE_PASS[4]=$PARSED_PASS
            SUITE_FAIL[4]=$PARSED_FAIL
        else
            SUITE_NOTES[4]="no results line"
        fi

        if [[ $mn_exit -eq 0 ]]; then
            SUITE_STATUS[4]="PASS"
        else
            SUITE_STATUS[4]="FAIL"
            OVERALL_FAIL=1
        fi
    fi
    echo ""
fi

# ===========================================================================
# STEP 5: MICROTEST
# ===========================================================================
if [[ $RUN_MICROTEST -eq 1 ]]; then
    echo -e "${C_BOLD}--- gbmicrotest Cycle-Accurate Tests ---${C_RESET}"
    MICROTEST_SCRIPT="$SCRIPT_DIR/run_microtest.sh"

    if [[ ! -f "$MICROTEST_SCRIPT" ]]; then
        echo "ERROR: run_microtest.sh not found"
        SUITE_STATUS[5]="FAIL"
        OVERALL_FAIL=1
    else
        set +e
        mt_output=$(bash "$MICROTEST_SCRIPT" "$BUILD_DIR" 2>&1)
        mt_exit=$?
        set -e

        printf '%s\n' "$mt_output" | sed 's/^/  /'
        echo ""

        if parse_results "$mt_output"; then
            SUITE_PASS[5]=$PARSED_PASS
            SUITE_FAIL[5]=$PARSED_FAIL
        else
            SUITE_NOTES[5]="no results line"
        fi

        if [[ $mt_exit -eq 0 ]]; then
            SUITE_STATUS[5]="PASS"
        else
            SUITE_STATUS[5]="FAIL"
            OVERALL_FAIL=1
        fi
    fi
    echo ""
fi

# ===========================================================================
# STEP 6: SAMESUITE
# ===========================================================================
if [[ $RUN_SAMESUITE -eq 1 ]]; then
    echo -e "${C_BOLD}--- SameSuite APU/DMA/PPU Tests ---${C_RESET}"
    SAMESUITE_SCRIPT="$SCRIPT_DIR/run_samesuite.sh"

    if [[ ! -f "$SAMESUITE_SCRIPT" ]]; then
        echo "ERROR: run_samesuite.sh not found"
        SUITE_STATUS[6]="FAIL"
        OVERALL_FAIL=1
    else
        set +e
        ss_output=$(bash "$SAMESUITE_SCRIPT" "$BUILD_DIR" 2>&1)
        ss_exit=$?
        set -e

        printf '%s\n' "$ss_output" | sed 's/^/  /'
        echo ""

        if parse_results "$ss_output"; then
            SUITE_PASS[6]=$PARSED_PASS
            SUITE_FAIL[6]=$PARSED_FAIL
        else
            SUITE_NOTES[6]="no results line"
        fi

        if [[ $ss_exit -eq 0 ]]; then
            SUITE_STATUS[6]="PASS"
        else
            SUITE_STATUS[6]="FAIL"
            OVERALL_FAIL=1
        fi
    fi
    echo ""
fi

# ===========================================================================
# SUMMARY TABLE
# ===========================================================================
echo -e "${C_BOLD}$SEP${C_RESET}"
printf "${C_BOLD}%s${C_RESET}\n" "                    TEST SUITE SUMMARY"
echo -e "${C_BOLD}$SEP${C_RESET}"
printf "${C_BOLD}%-16s  %-4s  %-9s  %-9s  %s${C_RESET}\n" \
    "Suite" "Status" "Passed" "Failed" "Notes"
echo "$DASH"

# Build
printf "%-16s  %b  %-9s  %-9s  %s\n" \
    "Build" "$(color_status "${SUITE_STATUS[0]}")" "-" "-" "${SUITE_NOTES[0]}"

# Blargg
if [[ "${SUITE_STATUS[1]}" != "SKIP" ]] || [[ "${SUITE_NOTES[1]}" == "binary missing" ]]; then
    btotal=$(( SUITE_PASS[1] + SUITE_FAIL[1] ))
    printf "%-16s  %b  %-9s  %-9s  %s\n" \
        "Blargg" "$(color_status "${SUITE_STATUS[1]}")" \
        "${SUITE_PASS[1]}/${btotal}" "${SUITE_FAIL[1]}/${btotal}" \
        "${SUITE_NOTES[1]}"
    for i in "${!BLARGG_ROM_LABELS[@]}"; do
        printf "  %-14s  %b\n" "${BLARGG_ROM_LABELS[$i]}" "$(color_status "${BLARGG_ROM_STATUS[$i]}")"
    done
else
    printf "%-16s  %b  %-9s  %-9s  %s\n" \
        "Blargg" "$(color_status SKIP)" "-" "-" "${SUITE_NOTES[1]}"
fi

# SM83
if [[ "${SUITE_STATUS[2]}" == "PASS" ]]; then
    printf "%-16s  %b  %-9s  %-9s  %s\n" \
        "SM83" "$(color_status PASS)" "500000/500000" "0/500000" ""
else
    printf "%-16s  %b  %-9s  %-9s  %s\n" \
        "SM83" "$(color_status "${SUITE_STATUS[2]}")" "-" "-" "${SUITE_NOTES[2]}"
fi

# Mealybug + Mooneye + Microtest + SameSuite
for idx in 3 4 5 6; do
    st="${SUITE_STATUS[$idx]}"
    if [[ "$st" == "SKIP" ]]; then
        printf "%-16s  %b  %-9s  %-9s  %s\n" \
            "${SUITE_NAMES[$idx]}" "$(color_status SKIP)" "-" "-" "${SUITE_NOTES[$idx]}"
    else
        total=$(( SUITE_PASS[$idx] + SUITE_FAIL[$idx] ))
        printf "%-16s  %b  %-9s  %-9s  %s\n" \
            "${SUITE_NAMES[$idx]}" "$(color_status "$st")" \
            "${SUITE_PASS[$idx]}/${total}" "${SUITE_FAIL[$idx]}/${total}" \
            "${SUITE_NOTES[$idx]}"
    fi
done

echo "$DASH"

total_pass=$(( SUITE_PASS[1] + SUITE_PASS[2] + SUITE_PASS[3] + SUITE_PASS[4] + SUITE_PASS[5] + SUITE_PASS[6] ))
total_fail=$(( SUITE_FAIL[1] + SUITE_FAIL[2] + SUITE_FAIL[3] + SUITE_FAIL[4] + SUITE_FAIL[5] + SUITE_FAIL[6] ))
overall_label="PASS"; [[ $OVERALL_FAIL -ne 0 ]] && overall_label="FAIL"

printf "${C_BOLD}%-16s  %b  %-9s  %-9s${C_RESET}\n" \
    "OVERALL" "$(color_status "$overall_label")" "$total_pass" "$total_fail"
echo -e "${C_BOLD}$SEP${C_RESET}"
echo ""

exit $OVERALL_FAIL
