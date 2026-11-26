#!/bin/bash
# Test-mode runner for a single DEM/Yade case.
#
# This is a supported entry point: it may be called directly on one DEM case,
# or by applications/test/regression/Allrun.yade for the whole DEM suite. The
# test-mode controls (run length, wall-clock bound) and the output assertions
# live here rather than in the tutorial Allrun scripts, so a tutorial stays a
# tutorial and this script stays the authority on what a DEM case must produce.
#
# Run length: the solver's controlDict sets it. The Yade script follows it
# (tutorials/cases/DEM_common/foamTime.py) and the coupling ends Yade on the
# solver's last step (#328); a run without PgfCoupling's line saying so FAILs.
#
# Usage:
#   runDEMCase.sh <caseDir> [--timeout SECONDS] [--run-dir DIR]
#                 [--short-steps N] [--rtol R] [--atol A] [--compare PATH]
#                 [--state-dir DIR] [--suite-events PATH]
#                 [--case-id ID] [--suite-id ID]
#
# Options:
#   --timeout S       Wall-clock bound for the run (overrides $YADE_TIMEOUT).
#   --run-dir DIR     Copy the case into DIR at its repo-relative path (siblings
#                     symlinked) and run it there, leaving the source case
#                     untouched; the baseline is still the source's reference/.
#   --short-steps N   Smoke run: set the copy's endTime to N solver steps and
#                     its writeInterval to N/2, and skip the baseline comparison
#                     (it holds full-length rows). Needs --run-dir.
#   --rtol R          Relative tolerance for the postProcessing comparator.
#   --atol A          Absolute tolerance for the postProcessing comparator.
#   --compare PATH    Comparator to use (default: sibling compareScalars.py).
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
#   YADE_TIMEOUT      wall-clock bound in seconds           (default 3600)
#   YADE_KILL_GRACE   TERM-to-KILL grace for the timeout    (default 30)
#
# postProcessing comparison: a case carrying a committed
# reference/postProcessing/ baseline has its run diffed against it with
# compareScalars.py. A case without one (e.g. MicroTGA-DEM_Us_UsInterp) skips
# the comparison rather than failing. Invariants declared in the case's
# system/regressionInvariants are checked first (tools/checkInvariants.py).
#
# Exit codes (shared outcome taxonomy — see regressionLib.sh):
#   0      - PASS  (ran to completion, all output asserts satisfied, and any
#                   postProcessing comparison was within tolerance)
#   1      - FAIL  (ran, but an output assert failed: no RUN FINISH / solver
#                   End / Yade not ended by the solver's last step / fewer
#                   than two VTK or solver write times / dt; a declared
#                   invariant broke; or postProcessing diverged from the
#                   reference baseline)
#   2      - ERROR (infrastructure: missing case dir, failed clean or copy)
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
RUN_DIR=""
SHORT_STEPS=""
RTOL="1e-4"
ATOL="1e-12"
COMPARE=""
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
    --run-dir)
        RUN_DIR="$2"
        shift 2
        ;;
    --short-steps)
        SHORT_STEPS="$2"
        shift 2
        ;;
    --nsteps)
        echo "ERROR: --nsteps was removed: the Yade step count now follows the case's controlDict (--short-steps shortens a copied run)" >&2
        exit 2
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
        if [ -z "$CASE_DIR" ]; then CASE_DIR="$1"; else
            echo "Unexpected positional: $1" >&2
            exit 2
        fi
        shift
        ;;
    esac
done

TIMEOUT="${OPT_TIMEOUT:-${YADE_TIMEOUT:-3600}}"
[ -n "$COMPARE" ] || COMPARE="$SCRIPT_DIR/compareScalars.py"
# Grace period between the timeout TERM and the follow-up KILL, to let MPI tear
# down cleanly before the group is force-killed.
KILL_GRACE="${YADE_KILL_GRACE:-30}"
# The run's own TMPDIR, removed by finish (empty until the run phase).
RUN_TMPDIR=""

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
    [ -n "$RUN_TMPDIR" ] && rm -rf "$RUN_TMPDIR"
    exit "$rc"
}

# -- validation ---------------------------------------------------------------

if [ -z "$CASE_DIR" ] || [ ! -d "$CASE_DIR" ]; then
    echo "ERROR: case directory not found: $CASE_DIR" >&2
    finish 2 "case directory not found: $CASE_DIR"
fi

CASE_DIR="$(cd "$CASE_DIR" && pwd)"

if [ -n "$SHORT_STEPS" ]; then
    if ! [[ "$SHORT_STEPS" =~ ^[0-9]+$ ]] || [ "$SHORT_STEPS" -lt 2 ]; then
        echo "ERROR: --short-steps must be an integer >= 2 (got: $SHORT_STEPS)" >&2
        finish 2 "--short-steps must be an integer >= 2 (got: $SHORT_STEPS)"
    fi
    if [ -z "$RUN_DIR" ]; then
        echo "ERROR: --short-steps edits controlDict, so it needs --run-dir (a copy)" >&2
        finish 2 "--short-steps needs --run-dir: it would edit the source case's controlDict"
    fi
fi

# WORK_DIR is where the case is cleaned, run and its output read; the baseline
# and invariants always come from the source case.
WORK_DIR="$CASE_DIR"
if [ -n "$RUN_DIR" ]; then
    REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
    stageRoot="$REPO_ROOT"
    caseRel="${CASE_DIR#"$REPO_ROOT"/}"
    if [ "$caseRel" = "$CASE_DIR" ]; then
        stageRoot="$(dirname "$CASE_DIR")"
        caseRel="$(basename "$CASE_DIR")"
    fi
    if ! WORK_DIR="$(reg_stage_case "$stageRoot" "$caseRel" "$RUN_DIR")"; then
        finish 2 "cannot copy $CASE_DIR into $RUN_DIR"
    fi
    echo "  run copy: $WORK_DIR"
    RESULT_FIELDS+=(--artifact "run_dir=$WORK_DIR")
fi
RESULT_FIELDS+=(
    --artifact "case_dir=$CASE_DIR"
    --artifact "run_log=$WORK_DIR/run.log"
)

cd "$WORK_DIR" || { echo "ERROR: cannot cd into $WORK_DIR" >&2; finish 2 "cannot cd into $WORK_DIR"; }

reg_event case_started \
    --field "case_id=$CASE_ID" \
    --field "suite_id=$SUITE_ID" \
    --field "case_dir=$CASE_DIR" \
    --field "runner=runDEMCase.sh" \
    --int "short_steps=${SHORT_STEPS:-0}" \
    --int "timeout_s=$TIMEOUT"

# -- clean --------------------------------------------------------------------

# Checked clean (no swallowed failure): a broken clean leaves a dirty slate that
# would corrupt the run's output asserts.
reg_phase clean
if [ -x ./Allclean ]; then
    if ! ./Allclean > /dev/null 2>&1; then
        echo "ERROR: Allclean failed in $WORK_DIR" >&2
        finish 2 "Allclean failed in $WORK_DIR"
    fi
fi

# -- shorten (--short-steps, on the copy only) ---------------------------------

if [ -n "$SHORT_STEPS" ]; then
    reg_phase shorten
    if ! msg="$(python3 "$SCRIPT_DIR/shortenControlDict.py" system/controlDict "$SHORT_STEPS" 2>&1)"; then
        echo "ERROR: $msg" >&2
        finish 2 "cannot shorten controlDict: $msg"
    fi
    echo "  short run: $msg"
fi

# -- run ----------------------------------------------------------------------

reg_phase run
echo "  running DEM simulation (timeout=${TIMEOUT}s)..."
# A killed mpirun leaves $TMPDIR/prterun.<host>.<pid>.<uid> behind, and the next
# mpirun given that pid (PID namespaces reuse them) fails with PMIX_ERR_EXISTS.
RUN_TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/pgf-dem-XXXXXX")" || finish 2 "cannot create the run's TMPDIR"
# Run the case in its own session/process group (setsid) under timeout, so a
# hung run and its whole tree (mpirun -> yade and solver ranks) are
# terminated by signalling THAT group only. This replaces a host-wide
# `pkill -f porousGasificationFoam`, which would kill unrelated solver runs
# elsewhere on the machine. No `|| true`: the run's exit status is classified.
# -w: when setsid leads its process group it forks, and must still wait.
# No bytecode: DEM_common is a symlink into the source tree.
TMPDIR="$RUN_TMPDIR" PYTHONDONTWRITEBYTECODE=1 \
setsid -w timeout --kill-after="$KILL_GRACE" "$TIMEOUT" bash ./Allrun > run.log 2>&1
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
grep -aqF "PgfCoupling: OpenFOAM ran its last step" run.log \
    || { echo "ERROR: Yade was not ended by the solver's last step (no 'PgfCoupling: OpenFOAM ran its last step' in run.log)"; finish 1 "Yade was not ended by the solver's last step"; }
grep -q "DEM coupling: active" run.log \
    || { echo "ERROR: DEM coupling not active — solver built without -DWITH_YADE=1?"; finish 1 "DEM coupling not active — solver built without -DWITH_YADE=1?"; }
grep -aqE '^(\[[0-9]+\] )?End$' run.log \
    || { echo "ERROR: the solver did not reach its endTime (no End in run.log)"; finish 1 "solver did not print End"; }
ls springs/springs_*.vtp > /dev/null 2>&1 || { echo "ERROR: no spring VTK files written"; finish 1 "no spring VTK files written"; }
# Sphere frame <iter> is the state at iter*deltaT (pgfView's rule). Every solver
# write, the start and end included, needs a frame, and every frame a write
# (or the run's last step, which --short-steps may not write).
if ! dem_out=$(python3 - <<'PYTIMES'
import glob, os, re, sys
d = "processor0" if os.path.isdir("processor0") else "."
solverFields = ("U", "p", "T")  # function-object output alone is not a solver write
writes = []
for name in os.listdir(d):
    try:
        t = float(name)
    except ValueError:
        continue
    if any(os.path.isfile(os.path.join(d, name, f)) for f in solverFields):
        writes.append(t)
dt = float(re.search(r"^\s*deltaT\s+([^;\s]+)\s*;", open("system/controlDict").read(), re.M).group(1))
frames = sorted(int(re.search(r"_(\d+)\.pvtu$", f).group(1)) * dt
                for f in glob.glob("spheres/vtk-spheres_*.pvtu"))
writes.sort()
nAfter = len(writes) - 1 if writes else 0
if nAfter < 2:  # a run that wrote only its initial state fails
    print(f"the solver wrote {nAfter} time(s) after the start, need at least 2"); sys.exit(1)
near = lambda t, ts: any(abs(t - u) <= 0.5 * dt for u in ts)
noFrame = [t for t in writes if not near(t, frames)]
noWrite = [t for t in frames if not near(t, writes) and t < max(frames)]
if noFrame or noWrite:
    fmt = lambda ts: " ".join(f"{t:g}" for t in ts) or "-"
    print(f"sphere frames do not match solver writes: no frame at {fmt(noFrame)}; no write at {fmt(noWrite)}"); sys.exit(1)
print(f"{len(frames)} sphere frames, {len(writes)} solver writes, start and end included")
PYTIMES
); then
    echo "ERROR: $dem_out"; finish 1 "$dem_out"
fi
echo "  DEM output check passed ($dem_out)"

RESULT_FIELDS+=(
    --artifact "spheres=$WORK_DIR/spheres"
    --artifact "springs=$WORK_DIR/springs"
    --artifact "dt_info=$WORK_DIR/dtInfo.txt"
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

# -- invariants (system/regressionInvariants, when present) --------------------
# Checked before the baseline, as in runCase.sh: a broken invariant is a bug
# whatever the baseline holds.

INVARIANTS="$CASE_DIR/system/regressionInvariants"
if [ -f "$INVARIANTS" ]; then
    reg_phase invariants
    python3 "$SCRIPT_DIR/checkInvariants.py" \
        --invariants "$INVARIANTS" \
        --postprocessing "$WORK_DIR/postProcessing"
    rc=$?
    case "$rc" in
        0) echo "  invariant check passed" ;;
        1) finish 1 "declared invariant broken (system/regressionInvariants)" ;;
        *) finish 2 "invariant checker reported an infrastructure error (exit $rc)" ;;
    esac
fi

# -- postProcessing comparison (only when a baseline is committed) -------------

if [ -n "$SHORT_STEPS" ] && [ -d "$CASE_DIR/reference/postProcessing" ]; then
    echo "  postProcessing comparison skipped: the baseline is full-length, this run is $SHORT_STEPS steps"
elif [ -d "$CASE_DIR/reference/postProcessing" ]; then
    reg_phase compare
    RESULT_FIELDS+=(
        --artifact "candidate=$WORK_DIR/postProcessing"
        --artifact "reference=$CASE_DIR/reference/postProcessing"
    )
    python3 "$COMPARE" \
        --reference "$CASE_DIR/reference/postProcessing" \
        --candidate "$WORK_DIR/postProcessing" \
        --rtol "$RTOL" \
        --atol "$ATOL"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "  postProcessing comparison passed"
    elif [ "$rc" -eq 1 ]; then
        finish 1 "postProcessing diverged from reference/postProcessing baseline"
    else
        finish 2 "comparator reported an infrastructure error (exit $rc)"
    fi
fi

if [ -n "$SHORT_STEPS" ]; then
    finish 0 "short DEM run ($SHORT_STEPS solver steps) completed; coupling, output and invariant assertions satisfied, no baseline comparison"
fi
finish 0 "DEM run completed; coupling, VTK, dtInfo and postProcessing assertions satisfied"
