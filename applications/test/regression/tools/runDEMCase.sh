#!/bin/bash
# Test-mode runner for a single DEM/Yade case.
#
# This is a supported entry point: it may be called directly on one DEM case,
# or by applications/test/regression/Allrun.yade for the whole DEM suite. The
# test-mode controls (step cap, wall-clock bound) and the output assertions live
# here rather than in the tutorial Allrun scripts, so a tutorial stays a
# tutorial and this script stays the authority on what a DEM case must produce.
#
# Usage:
#   runDEMCase.sh <caseDir> [--timeout SECONDS] [--nsteps N]
#                 [--state-dir DIR] [--suite-events PATH]
#                 [--case-id ID] [--suite-id ID]
#
# Options:
#   --timeout S       Wall-clock bound for the run (overrides $YADE_TIMEOUT).
#   --nsteps N        DEM step cap (overrides $YADE_NSTEPS).
#   --state-dir DIR   Write structured run state for this case into DIR
#                     (state.json, events.ndjson, result.json). Optional:
#                     without it nothing extra is written and the script behaves
#                     exactly as it always has.
#   --suite-events P  Additionally append this case's events to the suite-level
#                     NDJSON stream at P, so one stream can be tailed.
#   --case-id ID      Case id recorded in state (default: the case basename).
#   --suite-id ID     Suite id recorded in state (default: empty).
#
# Environment (a CLI option wins over the variable):
#   YADE_NSTEPS       DEM step cap                          (default 2000000)
#   YADE_TIMEOUT      wall-clock bound in seconds           (default 3600)
#   YADE_KILL_GRACE   TERM-to-KILL grace for the timeout    (default 30)
#
# Exit codes (shared outcome taxonomy — see regressionLib.sh):
#   0      - PASS  (ran to completion, all output asserts satisfied)
#   1      - FAIL  (ran, but an output assert failed: no RUN FINISH / VTK / dt)
#   2      - ERROR (infrastructure: missing case dir, failed clean)
#   124    - TIMEOUT (the run exceeded the timeout and its process group was
#                     terminated)
#   128+N  - CRASH (the run process group died on signal N)
#
# The exit code is the contract. The JSON written under --state-dir is an
# additional representation of the same outcome, never a substitute for it.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Shared outcome taxonomy + run-state helpers (reg_state, reg_event).
# shellcheck disable=SC1091
. "$SCRIPT_DIR/regressionLib.sh"

CASE_DIR=""
OPT_TIMEOUT=""
OPT_NSTEPS=""
STATE_DIR=""
SUITE_EVENTS=""
CASE_ID=""
SUITE_ID=""

while [ $# -gt 0 ]; do
    case "$1" in
    --timeout)
        OPT_TIMEOUT="$2"
        shift 2
        ;;
    --nsteps)
        OPT_NSTEPS="$2"
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
        if [ -z "$CASE_DIR" ]; then CASE_DIR="$1"; else
            echo "Unexpected positional: $1" >&2
            exit 2
        fi
        shift
        ;;
    esac
done

NSTEPS="${OPT_NSTEPS:-${YADE_NSTEPS:-2000000}}"
TIMEOUT="${OPT_TIMEOUT:-${YADE_TIMEOUT:-3600}}"
# Grace period between the timeout TERM and the follow-up KILL, to let MPI tear
# down cleanly before the group is force-killed.
KILL_GRACE="${YADE_KILL_GRACE:-30}"

# -- run-state plumbing (inert unless --state-dir was given) -------------------

CASE_STATE=""
CASE_RESULT=""
PHASE="queued"
T_START=$(date +%s)
RESULT_FIELDS=()

[ -n "$CASE_ID" ] || CASE_ID="$(basename "${CASE_DIR:-unknown}")"

if [ -n "$STATE_DIR" ]; then
    if ! mkdir -p "$STATE_DIR"; then
        echo "ERROR: cannot create state directory: $STATE_DIR" >&2
        exit 2
    fi
    CASE_STATE="$STATE_DIR/state.json"
    CASE_RESULT="$STATE_DIR/result.json"
    REG_EVENT_STREAMS=("$STATE_DIR/events.ndjson")
    [ -n "$SUITE_EVENTS" ] && REG_EVENT_STREAMS+=("$SUITE_EVENTS")
fi

# reg_phase <phase> — record entry into a lifecycle phase (clean, run, assert).
reg_phase() {
    PHASE="$1"
    reg_event case_phase --field "case_id=$CASE_ID" --field "phase=$1"
    [ -n "$CASE_STATE" ] || return 0
    reg_state snapshot --path "$CASE_STATE" \
        --field "case_id=$CASE_ID" \
        --field "suite_id=$SUITE_ID" \
        --field "phase=$1" \
        --field "case_dir=$CASE_DIR" \
        --field "runner=runDEMCase.sh" \
        --num "started_epoch=$T_START"
}

# finish <exit-code> [message] — write the terminal result, then exit with it.
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
            --runner runDEMCase.sh \
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

if [ -z "$CASE_DIR" ] || [ ! -d "$CASE_DIR" ]; then
    echo "ERROR: case directory not found: $CASE_DIR" >&2
    finish 2 "case directory not found: $CASE_DIR"
fi

CASE_DIR="$(cd "$CASE_DIR" && pwd)"
RESULT_FIELDS+=(
    --artifact "case_dir=$CASE_DIR"
    --artifact "run_log=$CASE_DIR/run.log"
)

cd "$CASE_DIR" || { echo "ERROR: cannot cd into $CASE_DIR" >&2; finish 2 "cannot cd into $CASE_DIR"; }

reg_event case_started \
    --field "case_id=$CASE_ID" \
    --field "suite_id=$SUITE_ID" \
    --field "case_dir=$CASE_DIR" \
    --field "runner=runDEMCase.sh" \
    --int "nsteps=$NSTEPS" \
    --int "timeout_s=$TIMEOUT"

# -- clean --------------------------------------------------------------------

# Checked clean (no swallowed failure): a broken clean leaves a dirty slate that
# would corrupt the run's output asserts.
reg_phase clean
if [ -x ./Allclean ]; then
    if ! ./Allclean > /dev/null 2>&1; then
        echo "ERROR: Allclean failed in $CASE_DIR" >&2
        finish 2 "Allclean failed in $CASE_DIR"
    fi
fi

# -- run ----------------------------------------------------------------------

reg_phase run
echo "  running DEM simulation (NSTEPS=$NSTEPS, timeout=${TIMEOUT}s)..."
# Run the case in its own session/process group (setsid) under timeout, so a
# hung run and its whole tree (mpirun -> yade -> MPI_Comm_spawn'd solver) are
# terminated by signalling THAT group only. This replaces a host-wide
# `pkill -f porousGasificationFoam`, which would kill unrelated solver runs
# elsewhere on the machine. No `|| true`: the run's exit status is classified.
YADE_NSTEPS="$NSTEPS" \
setsid timeout --kill-after="$KILL_GRACE" "$TIMEOUT" bash ./Allrun > run.log 2>&1
rc=$?
if [ "$rc" -eq 124 ]; then
    echo "ERROR: DEM run exceeded ${TIMEOUT}s — process group terminated (TIMEOUT)"
    finish 124 "DEM run exceeded ${TIMEOUT}s — process group terminated"
elif [ "$rc" -gt 128 ]; then
    echo "ERROR: DEM run died on signal $(( rc - 128 )) (CRASH)"
    finish "$rc" "DEM run died on signal $(( rc - 128 )) ($(reg_signal_name $(( rc - 128 ))))"
elif [ "$rc" -ne 0 ]; then
    echo "ERROR: DEM run launcher exited non-zero ($rc) — check run.log"
    finish "$rc" "DEM run launcher exited non-zero ($rc) — check run.log"
fi

# -- output assertions --------------------------------------------------------

reg_phase assert

grep -q "RUN FINISH" run.log \
    || { echo "ERROR: simulation did not complete — check run.log"; finish 1 "simulation did not complete (no RUN FINISH in run.log)"; }
grep -q "DEM coupling: active" run.log \
    || { echo "ERROR: DEM coupling not active — solver built without -DWITH_YADE=1?"; finish 1 "DEM coupling not active — solver built without -DWITH_YADE=1?"; }
ls spheres/spheres_*.vtp > /dev/null 2>&1 || { echo "ERROR: no sphere VTK files written"; finish 1 "no sphere VTK files written"; }
ls springs/springs_*.vtp > /dev/null 2>&1 || { echo "ERROR: no spring VTK files written"; finish 1 "no spring VTK files written"; }
echo "  DEM output check passed"

RESULT_FIELDS+=(
    --artifact "spheres=$CASE_DIR/spheres"
    --artifact "springs=$CASE_DIR/springs"
    --artifact "dt_info=$CASE_DIR/dtInfo.txt"
)

[ -f dtInfo.txt ] \
    || { echo "ERROR: dtInfo.txt not written — did the run reach iter 1?"; finish 1 "dtInfo.txt not written — did the run reach iter 1?"; }
if ! python3 - <<'PYCHECK'
import sys
lines = [l for l in open("dtInfo.txt") if not l.startswith("iter") and l.strip()]
if not lines:
    print("ERROR: dtInfo.txt empty — coupling did not reach iter 1"); sys.exit(1)
foamDt = float(lines[0].split()[3])
if foamDt <= 0.0:
    print("ERROR: foamDt={:.3e} — PGF time step not received".format(foamDt)); sys.exit(1)
print("  PGF coupling check passed (foamDt={:.3e})".format(foamDt))
PYCHECK
then finish 1 "dtInfo.txt assertion failed — no positive PGF time step received"; fi

finish 0 "DEM run completed; coupling, VTK and dtInfo assertions satisfied"
