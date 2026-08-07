#!/bin/bash
# Run one regression case and compare its postProcessing/ output against the
# committed reference/postProcessing/ baseline.
#
# This is a supported entry point: it may be called directly on a single case,
# or by applications/test/regression/Allrun for a whole suite. Either way this
# script — not its caller, and not any external tool — owns the serial case
# lifecycle: the checked Allclean, the clean-slate assertion, the case's own
# ./Allrun, and the numerical comparison.
#
# Usage:
#   runCase.sh <caseDir> [--no-run] [--rtol R] [--atol A] [--compare PATH]
#              [--state-dir DIR] [--suite-events PATH]
#              [--case-id ID] [--suite-id ID]
#
# Options:
#   --no-run          Skip cleaning and running; compare existing output only.
#   --rtol R          Relative tolerance for the comparator.
#   --atol A          Absolute tolerance for the comparator.
#   --compare PATH    Comparator to use (default: sibling compareScalars.py).
#   --state-dir DIR   Write structured run state for this case into DIR
#                     (state.json, events.ndjson, result.json, compare logs).
#                     Optional: without it nothing extra is written and the
#                     script behaves exactly as it always has.
#   --suite-events P  Additionally append this case's events to the suite-level
#                     NDJSON stream at P, so one stream can be tailed.
#   --case-id ID      Case id recorded in state (default: the case basename).
#   --suite-id ID     Suite id recorded in state (default: empty).
#
# Exit codes (shared outcome taxonomy — see tools/regressionLib.sh):
#   0      - PASS  (ran, compared, within tolerance)
#   1      - FAIL  (ran, compared, numerical divergence)
#   2      - ERROR (infrastructure: missing files, dirty slate, run failure)
#   128+N  - CRASH (the case Allrun/solver died on signal N; propagated as-is so
#                   the runner reports e.g. CRASH (SIGABRT) for a 134 abort)
#
# The exit code is the contract. The JSON written under --state-dir is an
# additional representation of the same outcome, never a substitute for it.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Shared outcome taxonomy + run-state helpers (reg_state, reg_event). The
# library sources helpers.sh internally.
# shellcheck disable=SC1091
. "$SCRIPT_DIR/regressionLib.sh"

caseDir=""
SKIP_RUN=false
RTOL="1e-4"
ATOL="1e-12"
COMPARE=""
STATE_DIR=""
SUITE_EVENTS=""
CASE_ID=""
SUITE_ID=""

while [ $# -gt 0 ]; do
    case "$1" in
    --no-run)
        SKIP_RUN=true
        shift
        ;;
    --rtol)
        RTOL="$2"
        shift 2
        ;;
    --atol)
        ATOL="$2"
        shift 2
        ;;
    --compare)
        COMPARE="$2"
        shift 2
        ;;
    --state-dir)
        STATE_DIR="$2"
        shift 2
        ;;
    --suite-events)
        SUITE_EVENTS="$2"
        shift 2
        ;;
    --case-id)
        CASE_ID="$2"
        shift 2
        ;;
    --suite-id)
        SUITE_ID="$2"
        shift 2
        ;;
    -*)
        echo "Unknown option: $1" >&2
        exit 2
        ;;
    *)
        if [ -z "$caseDir" ]; then caseDir="$1"; else
            echo "Unexpected positional: $1" >&2
            exit 2
        fi
        shift
        ;;
    esac
done

# -- run-state plumbing (inert unless --state-dir was given) -------------------

CASE_STATE=""
CASE_RESULT=""
COMPARE_LOG=""
COMPARE_ERR=""
PHASE="queued"
T_START=$(date +%s)
RESULT_FIELDS=()

[ -n "$CASE_ID" ] || CASE_ID="$(basename "${caseDir:-unknown}")"

if [ -n "$STATE_DIR" ]; then
    if ! mkdir -p "$STATE_DIR"; then
        echo "[runCase] cannot create state directory: $STATE_DIR" >&2
        exit 2
    fi
    CASE_STATE="$STATE_DIR/state.json"
    CASE_RESULT="$STATE_DIR/result.json"
    COMPARE_LOG="$STATE_DIR/compare.log"
    COMPARE_ERR="$STATE_DIR/compare.err.log"
    REG_EVENT_STREAMS=("$STATE_DIR/events.ndjson")
    [ -n "$SUITE_EVENTS" ] && REG_EVENT_STREAMS+=("$SUITE_EVENTS")
fi

# reg_phase <phase> — record entry into a lifecycle phase.
#   Phases for this runner: clean, run, assert, compare (plus the queued state
#   before the first one). The phase is carried into the terminal result so a
#   failure says *where* it happened, not just that it happened.
reg_phase() {
    PHASE="$1"
    reg_event case_phase --field "case_id=$CASE_ID" --field "phase=$1"
    [ -n "$CASE_STATE" ] || return 0
    reg_state snapshot --path "$CASE_STATE" \
        --field "case_id=$CASE_ID" \
        --field "suite_id=$SUITE_ID" \
        --field "phase=$1" \
        --field "case_dir=$caseDir" \
        --field "runner=runCase.sh" \
        --num "started_epoch=$T_START"
}

# finish <exit-code> [message] — write the terminal result, then exit with the
#   code. Every exit path goes through here so no path can end without either a
#   result file or (when the process dies outright) the driver noticing that one
#   is missing.
finish() {
    local rc="$1" msg="${2:-}" t_end
    t_end=$(date +%s)
    if [ -n "$CASE_RESULT" ]; then
        reg_state result --path "$CASE_RESULT" \
            --exit-code "$rc" \
            --case-id "$CASE_ID" \
            --suite-id "$SUITE_ID" \
            --phase "$PHASE" \
            --message "$msg" \
            --runner runCase.sh \
            --started-epoch "$T_START" \
            --ended-epoch "$t_end" \
            "${RESULT_FIELDS[@]}"
    fi
    reg_event case_finished \
        --field "case_id=$CASE_ID" \
        --int "exit_code=$rc" \
        --field "status=$(reg_classify "$rc")" \
        --field "phase=$PHASE" \
        --field "message=$msg"
    exit "$rc"
}

# -- validation ---------------------------------------------------------------

if [ -z "$caseDir" ] || [ ! -d "$caseDir" ]; then
    echo "Case directory not found: $caseDir" >&2
    finish 2 "case directory not found: $caseDir"
fi

caseDir="$(cd "$caseDir" && pwd)"
RESULT_FIELDS+=(--artifact "case_dir=$caseDir")
[ -n "$COMPARE_LOG" ] && RESULT_FIELDS+=(--artifact "compare_log=$COMPARE_LOG")

if [ -z "$COMPARE" ]; then
    COMPARE="$SCRIPT_DIR/compareScalars.py"
fi

if [ ! -x "$COMPARE" ] && [ ! -f "$COMPARE" ]; then
    echo "Comparator not found: $COMPARE" >&2
    finish 2 "comparator not found: $COMPARE"
fi

reg_event case_started \
    --field "case_id=$CASE_ID" \
    --field "suite_id=$SUITE_ID" \
    --field "case_dir=$caseDir" \
    --field "runner=runCase.sh" \
    --bool "no_run=$SKIP_RUN" \
    --field "rtol=$RTOL" \
    --field "atol=$ATOL"

# -- clean + run --------------------------------------------------------------

if [ "$SKIP_RUN" = false ]; then
    if [ ! -x "$caseDir/Allrun" ]; then
        echo "$caseDir/Allrun is missing or not executable" >&2
        finish 2 "$caseDir/Allrun is missing or not executable"
    fi

    # Clean prior run artefacts (preserve reference/). A swallowed clean can
    # leave a dirty slate that silently corrupts the comparison, so the clean
    # is checked and the slate is asserted afterwards.
    reg_phase clean
    if [ -x "$caseDir/Allclean" ]; then
        if ! (cd "$caseDir" && ./Allclean) >/dev/null 2>&1; then
            echo "[runCase] Allclean failed in $caseDir" >&2
            finish 2 "Allclean failed in $caseDir"
        fi
    fi
    if ls "$caseDir"/log.* >/dev/null 2>&1 || [ -d "$caseDir/postProcessing" ]; then
        echo "[runCase] dirty slate: log.*/postProcessing survived the clean in $caseDir" >&2
        finish 2 "dirty slate: log.*/postProcessing survived the clean in $caseDir"
    fi

    reg_phase run
    echo "[runCase] Running $caseDir/Allrun"
    (cd "$caseDir" && ./Allrun)
    rc=$?
    # Propagate the case's own exit status so the runner can classify it: a
    # signal death (128+N) surfaces as CRASH, any other non-zero as the case's
    # reported failure. Infrastructure problems (a broken/absent clean, a dirty
    # slate, missing files) are the runner's own concern and exit 2 above.
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -gt 128 ]; then
            echo "[runCase] Allrun in $caseDir died on signal $(( rc - 128 )) (exit $rc)" >&2
            finish "$rc" "Allrun died on signal $(( rc - 128 )) ($(reg_signal_name $(( rc - 128 ))))"
        else
            echo "[runCase] Allrun in $caseDir exited non-zero ($rc)" >&2
            finish "$rc" "Allrun exited non-zero ($rc)"
        fi
    fi
fi

# -- output assertions --------------------------------------------------------

reg_phase assert

if [ ! -d "$caseDir/postProcessing" ]; then
    echo "[runCase] No postProcessing/ produced under $caseDir" >&2
    finish 2 "no postProcessing/ produced under $caseDir"
fi

if [ ! -d "$caseDir/reference/postProcessing" ]; then
    echo "[runCase] No reference baseline at $caseDir/reference/postProcessing" >&2
    finish 2 "no reference baseline at $caseDir/reference/postProcessing"
fi

RESULT_FIELDS+=(
    --artifact "candidate=$caseDir/postProcessing"
    --artifact "reference=$caseDir/reference/postProcessing"
)

# -- comparison ---------------------------------------------------------------

reg_phase compare

if [ -n "$STATE_DIR" ]; then
    # State mode captures the comparator's streams so the counts it reports are
    # machine-readable, then replays them so the terminal output is unchanged.
    # No pipe: the comparator's exit status stays this script's own.
    python3 "$COMPARE" \
        --reference "$caseDir/reference/postProcessing" \
        --candidate "$caseDir/postProcessing" \
        --rtol "$RTOL" \
        --atol "$ATOL" >"$COMPARE_LOG" 2>"$COMPARE_ERR"
    rc=$?
    cat "$COMPARE_LOG"
    cat "$COMPARE_ERR" >&2

    # "compareScalars.py: <within>/<total> files within tolerance (...)" and,
    # when applicable, "compareScalars.py: <n> reference file(s) not present".
    nTotal=0; nWithin=0; nMissing=0
    line="$(grep -m1 'files within tolerance' "$COMPARE_LOG" || true)"
    if [[ "$line" =~ ([0-9]+)/([0-9]+)[[:space:]]+files[[:space:]]+within ]]; then
        nWithin="${BASH_REMATCH[1]}"
        nTotal="${BASH_REMATCH[2]}"
    fi
    line="$(grep -m1 'not present in candidate' "$COMPARE_LOG" || true)"
    if [[ "$line" =~ ([0-9]+)[[:space:]]+reference[[:space:]]+file ]]; then
        nMissing="${BASH_REMATCH[1]}"
    fi
    nFailed=$(( nTotal - nWithin - nMissing ))
    [ "$nFailed" -lt 0 ] && nFailed=0
    RESULT_FIELDS+=(
        --int "compare_files_total=$nTotal"
        --int "compare_files_passed=$nWithin"
        --int "compare_files_failed=$nFailed"
        --int "compare_files_missing=$nMissing"
    )
    if [ "$rc" -eq 0 ]; then
        msg="comparison within tolerance ($nWithin/$nTotal files)"
    elif [ "$rc" -eq 1 ]; then
        msg="comparison diverged ($nFailed out of tolerance, $nMissing missing of $nTotal)"
    else
        msg="comparator reported an infrastructure error (exit $rc)"
    fi
    finish "$rc" "$msg"
fi

python3 "$COMPARE" \
    --reference "$caseDir/reference/postProcessing" \
    --candidate "$caseDir/postProcessing" \
    --rtol "$RTOL" \
    --atol "$ATOL"
finish $? ""
