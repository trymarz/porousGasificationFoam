#!/bin/bash
# regressionLib.sh — shared logic for the PGF regression runners.
#
# Source-only library, sibling to runCase.sh / runDEMCase.sh. It sources
# helpers.sh (for clog) so each driver (Allrun, Allrun.yade) sources only this
# file. It holds the pieces that must NOT drift between the serial and DEM
# runners: how a suite is loaded, how a child exit code becomes an outcome, and
# how the summary decides whether the whole suite is green.
#
# Outcome taxonomy (child exit code -> label -> green?), standard shell
# conventions:
#
#   0        PASS    — ran, compared, within tolerance                    (green)
#   1        FAIL    — ran, compared, diverged                            (not green)
#   2        ERROR   — infrastructure (missing dir/baseline, dirty slate,
#                      no postProcessing/, comparator missing)            (not green)
#   124      TIMEOUT — propagated from timeout(1)                         (not green)
#   128+N    CRASH   — child died on signal N (139=SIGSEGV, 134=SIGABRT…) (not green)
#   other    ERROR   — any other non-zero code                           (not green)
#
# The only green suite is one where every recorded case PASSed. The empty-suite
# guard (an empty registered suite is never green) is enforced by the driver via
# reg_load_cases; see each Allrun.

# Source-only: refuse direct execution.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "regressionLib.sh is a source-only library — do not execute it directly." >&2
    echo "It is sourced by applications/test/regression/{Allrun,Allrun.yade}." >&2
    exit 1
fi

# Include guard ($REGRESSIONLIB_SOURCED is written with a :- default so the
# guard itself is safe under the drivers' `set -u`).
[[ -n "${REGRESSIONLIB_SOURCED:-}" ]] && return 0
REGRESSIONLIB_SOURCED=1

# Pull in clog. helpers.sh uses an unguarded $HELPERS_SOURCED include guard, so
# disable -u across the source (this is the dance the drivers used to repeat
# inline; it now lives in exactly one place).
_REGLIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_REGLIB_REPO_ROOT="$(cd "$_REGLIB_DIR/../../../.." && pwd)"
# shellcheck disable=SC1091
set +u; . "$_REGLIB_REPO_ROOT/utilities/bash_utils/helpers.sh"; set -u

# reg_load_cases <cases-file> <array-var-name>
#   Read the non-comment, non-blank lines of <cases-file> into the named array.
#   Return 0 if at least one case was loaded, 1 if the suite is empty (file
#   present but all comments/blank), 2 if the file is missing. The caller
#   decides what an empty suite means — the honest-exit rule (empty registered
#   suite is an ERROR, never green) lives in the driver so the transitional
#   behaviour is visible in the diff.
reg_load_cases() {
    local _file="$1" _arr="$2"
    if [ ! -f "$_file" ]; then
        clog ERROR "Cases file not found: $_file"
        return 2
    fi
    mapfile -t "$_arr" < <(grep -vE '^\s*(#|$)' "$_file")
    local -n _ref="$_arr"
    [ "${#_ref[@]}" -gt 0 ]
}

# reg_signal_name <n> — conventional name for a signal number (one table).
reg_signal_name() {
    case "$1" in
        6)  echo "SIGABRT" ;;
        9)  echo "SIGKILL" ;;
        11) echo "SIGSEGV" ;;
        15) echo "SIGTERM" ;;
        *)  echo "SIG$1"   ;;
    esac
}

# reg_classify <rc> — echo the outcome label for a child exit code, per the
# taxonomy above.
reg_classify() {
    local rc="$1"
    if [ "$rc" -eq 0 ]; then
        echo "PASS"
    elif [ "$rc" -eq 1 ]; then
        echo "FAIL"
    elif [ "$rc" -eq 2 ]; then
        echo "ERROR"
    elif [ "$rc" -eq 124 ]; then
        echo "TIMEOUT"
    elif [ "$rc" -gt 128 ]; then
        echo "CRASH ($(reg_signal_name $(( rc - 128 ))))"
    else
        echo "ERROR"
    fi
}

# Library-owned accumulators. A single run sources the library once and records
# into these; no reset is needed (and none is offered) for a single suite pass.
REG_NAMES=()
REG_RCS=()
REG_TIMES=()

# reg_record <name> <rc> <time-string> — accumulate one case outcome.
reg_record() {
    REG_NAMES+=("$1")
    REG_RCS+=("$2")
    REG_TIMES+=("$3")
}

# reg_summary [<title>] — print the outcome table + counts and return 0 iff at
# least one case was recorded and every recorded case PASSed. Nothing recorded
# (e.g. a filter that matched no case) is never green — a gate that compared
# nothing must not report success.
reg_summary() {
    local title="${1:-regression summary}"
    local n="${#REG_NAMES[@]}"
    local nPass=0 nFail=0 nError=0 nTimeout=0 nCrash=0
    local i rc label

    echo
    clog INFO "=================================================="
    clog INFO "  $title   ($n cases)"
    clog INFO "=================================================="

    # PASS rows first, then the non-green rows labelled by class.
    i=0
    while [ "$i" -lt "$n" ]; do
        if [ "${REG_RCS[$i]}" -eq 0 ]; then
            clog SUCCESS "  PASS      ${REG_TIMES[$i]}   ${REG_NAMES[$i]}"
            nPass=$(( nPass + 1 ))
        fi
        i=$(( i + 1 ))
    done

    i=0
    while [ "$i" -lt "$n" ]; do
        rc="${REG_RCS[$i]}"
        if [ "$rc" -ne 0 ]; then
            label="$(reg_classify "$rc")"
            clog ERROR "  ${label}   ${REG_TIMES[$i]}   ${REG_NAMES[$i]}"
            if [ "$rc" -eq 1 ]; then
                nFail=$(( nFail + 1 ))
            elif [ "$rc" -eq 124 ]; then
                nTimeout=$(( nTimeout + 1 ))
            elif [ "$rc" -gt 128 ]; then
                nCrash=$(( nCrash + 1 ))
            else
                nError=$(( nError + 1 ))
            fi
        fi
        i=$(( i + 1 ))
    done

    echo
    local counts="  passed: $nPass   failed: $nFail"
    [ "$nError"   -gt 0 ] && counts="$counts   error: $nError"
    [ "$nTimeout" -gt 0 ] && counts="$counts   timeout: $nTimeout"
    [ "$nCrash"   -gt 0 ] && counts="$counts   crash: $nCrash"
    clog INFO "$counts"
    clog INFO "=================================================="

    [ "$n" -gt 0 ] && [ "$nPass" -eq "$n" ]
}
