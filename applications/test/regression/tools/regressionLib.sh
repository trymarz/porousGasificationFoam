#!/bin/bash
# regressionLib.sh — shared logic for the PGF regression runners.
#
# Source-only library, sourced by both suite drivers (Allrun, Allrun.yade) and
# both per-case runners (runCase.sh, runDEMCase.sh). It sources helpers.sh (for
# clog) so a caller sources only this file. It holds the pieces that must NOT
# drift between the serial and DEM runners: how a suite is loaded, how a child
# exit code becomes an outcome, how the summary decides whether the whole suite
# is green, and how run state is serialised for an external observer.
#
# What it deliberately does NOT hold: the serial and DEM runners' own argument
# parsing, cleanup contracts and output assertions. Those differ, and collapsing
# them into one mode-switching script would hide exactly the details a
# regression runner has to get right.
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
    echo "It is sourced by applications/test/regression/{Allrun,Allrun.yade} and" >&2
    echo "by tools/{runCase.sh,runDEMCase.sh}." >&2
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
# Kept in step with regressionState.py's signal_name() so the printed summary
# and the JSON result never disagree about what killed a case.
reg_signal_name() {
    case "$1" in
        1)  echo "SIGHUP"  ;;
        2)  echo "SIGINT"  ;;
        3)  echo "SIGQUIT" ;;
        4)  echo "SIGILL"  ;;
        6)  echo "SIGABRT" ;;
        8)  echo "SIGFPE"  ;;
        9)  echo "SIGKILL" ;;
        11) echo "SIGSEGV" ;;
        13) echo "SIGPIPE" ;;
        14) echo "SIGALRM" ;;
        15) echo "SIGTERM" ;;
        *)  echo "SIG$1"   ;;
    esac
}

# -- Structured run-state protocol (optional) ---------------------------------
#
# The runners' exit codes remain the contract for shell callers; the JSON below
# is an additional representation for an external observer (CI, a TUI). It is
# entirely opt-in: with no state directory configured, every helper here is a
# no-op and the drivers behave exactly as they did before.
#
# Serialisation lives in tools/regressionState.py (standard library only, no
# project imports). These wrappers exist so a caller never has to build helper
# argv by hand, and so a *reporting* failure can never be mistaken for a case
# outcome — see reg_state below.

REG_STATE_PY="$_REGLIB_DIR/regressionState.py"

# Streams that reg_event appends to. A caller sets this to the event files it
# wants (case stream, suite stream, or both); empty disables event emission.
REG_EVENT_STREAMS=()

# reg_state <subcommand> [args...] — call the state helper.
#   Always returns 0. A state write that fails is a reporting problem, so it
#   warns and carries on: turning it into a non-zero status here would corrupt
#   the outcome of the case being reported on.
reg_state() {
    [ -f "$REG_STATE_PY" ] || return 0
    if ! python3 "$REG_STATE_PY" "$@"; then
        echo "[regression] warning: run-state write failed ($1)" >&2
    fi
    return 0
}

# reg_event <type> [extra helper args...] — append one event to every stream in
#   REG_EVENT_STREAMS. A no-op when no stream is configured.
reg_event() {
    [ "${#REG_EVENT_STREAMS[@]}" -gt 0 ] || return 0
    local _type="$1"; shift
    local _args=() _stream
    for _stream in "${REG_EVENT_STREAMS[@]}"; do
        _args+=(--stream "$_stream")
    done
    reg_state event --type "$_type" "${_args[@]}" "$@"
}

# -- Bounded case scheduling --------------------------------------------------
#
# Shared because getting it wrong is how a suite silently runs a case twice or
# loses a child's status — but deliberately *only* the mechanics. Which cases
# exist, which are runnable, and what running one means stay with each driver.
#
# The pool is <jobs> worker subshells pulling from one list of case indices. A
# worker claims a case with `mkdir <case-state>/.claim`, which either creates the
# directory or fails, so exactly one worker can win each case on any POSIX
# filesystem — no lock file, no lock daemon, no second scheduler to keep in step.
# Each worker records its case's elapsed time and then its exit status; the
# status file is written last, so its presence means the case really finished.
# The caller reads those files back in its own order after every worker has been
# waited for, which is what makes the summary deterministic even though cases
# finish in whatever order they finish.
#
# Workers stay in the caller's process group, so an interactive Ctrl-C reaches
# every case and its children exactly as it does for a sequential run. Nothing
# here signals by process name, and nothing here terminates anything: a case's
# own runner owns any timeout it needs, and scopes it to its own process group.

# reg_run_pool <jobs> <cases-root> <ids-var> <indices-var> <run-fn>
#   <ids-var>     name of an array of case ids, indexed as the driver indexes them
#   <indices-var> name of an array of indices into <ids-var> to run
#   <run-fn>      driver function taking one index and returning the case status
reg_run_pool() {
    local _jobs="$1" _root="$2"
    local -n _ids="$3"
    local -n _todo="$4"
    local _runfn="$5"
    local _slot _pids=() _pid

    for _slot in $(seq 1 "$_jobs"); do
        (
            for w_idx in "${_todo[@]}"; do
                w_cs="$_root/${_ids[$w_idx]}"
                mkdir "$w_cs/.claim" 2>/dev/null || continue
                w_t0=$(date +%s)
                "$_runfn" "$w_idx"
                w_rc=$?
                w_secs=$(( $(date +%s) - w_t0 ))
                printf '%s\n' "$w_secs" > "$w_cs/secs"
                printf '%s\n' "$w_rc"   > "$w_cs/rc"
                # One short line per completion: a single write, so concurrent
                # workers cannot tear each other's progress output.
                printf '  %-7s %5s   %s\n' \
                    "$(reg_classify "$w_rc")" "${w_secs}s" "${_ids[$w_idx]}"
            done
        ) &
        _pids+=("$!")
    done

    # Wait for every launched worker before the caller reads any status back, so
    # no case can still be writing its result when the summary is assembled.
    for _pid in "${_pids[@]}"; do
        wait "$_pid" || true
    done
}

# reg_pool_status <case-state-dir> — echo the exit status a worker recorded.
#   Echoes 2 when there is none: a worker that died without recording a status
#   is an infrastructure ERROR, and an absent status is never read as a pass.
reg_pool_status() {
    if [ -f "$1/rc" ]; then cat "$1/rc"; else echo 2; fi
}

# reg_pool_seconds <case-state-dir> — echo the elapsed seconds a worker recorded,
#   or "?" when the worker never got that far.
reg_pool_seconds() {
    if [ -f "$1/secs" ]; then cat "$1/secs"; else echo "?"; fi
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
