#!/bin/bash
# Test-mode runner for a single DEM/Yade case. Called by Allrun.yade — not for direct use.
# Usage: runDEMCase.sh <abs-path-to-case-dir>
#
# Exit codes (shared outcome taxonomy — see regressionLib.sh):
#   0      - PASS  (ran to completion, all output asserts satisfied)
#   1      - FAIL  (ran, but an output assert failed: no RUN FINISH / VTK / dt)
#   124    - TIMEOUT (the run exceeded YADE_TIMEOUT and its process group was
#                     terminated)
#   128+N  - CRASH (the run process group died on signal N)
set -u

CASE_DIR="${1:-}"
if [ -z "$CASE_DIR" ] || [ ! -d "$CASE_DIR" ]; then
    echo "ERROR: case directory not found: $CASE_DIR" >&2
    exit 2
fi

NSTEPS="${YADE_NSTEPS:-2000000}"
TIMEOUT="${YADE_TIMEOUT:-3600}"
# Grace period between the timeout TERM and the follow-up KILL, to let MPI tear
# down cleanly before the group is force-killed.
KILL_GRACE="${YADE_KILL_GRACE:-30}"

cd "$CASE_DIR" || { echo "ERROR: cannot cd into $CASE_DIR" >&2; exit 2; }

# Checked clean (no swallowed failure): a broken clean leaves a dirty slate that
# would corrupt the run's output asserts.
if [ -x ./Allclean ]; then
    if ! ./Allclean > /dev/null 2>&1; then
        echo "ERROR: Allclean failed in $CASE_DIR" >&2
        exit 2
    fi
fi

echo "  running DEM simulation (NSTEPS=$NSTEPS, timeout=${TIMEOUT}s)..."
# Run the case in its own session/process group (setsid) under timeout, so a
# hung run and its whole tree (mpirun -> yade -> MPI_Comm_spawn'd solver) are
# terminated by signalling THAT group only. This replaces a host-wide
# `pkill -f porousGasificationFoam`, which would kill unrelated solver runs
# elsewhere on the machine. No `|| true`: the run's exit status is classified.
setsid timeout --kill-after="$KILL_GRACE" "$TIMEOUT" bash ./Allrun > run.log 2>&1
rc=$?
if [ "$rc" -eq 124 ]; then
    echo "ERROR: DEM run exceeded ${TIMEOUT}s — process group terminated (TIMEOUT)"
    exit 124
elif [ "$rc" -gt 128 ]; then
    echo "ERROR: DEM run died on signal $(( rc - 128 )) (CRASH)"
    exit "$rc"
elif [ "$rc" -ne 0 ]; then
    echo "ERROR: DEM run launcher exited non-zero ($rc) — check run.log"
    exit "$rc"
fi

grep -q "RUN FINISH" run.log \
    || { echo "ERROR: simulation did not complete — check run.log"; exit 1; }
grep -q "DEM coupling: active" run.log \
    || { echo "ERROR: DEM coupling not active — solver built without -DWITH_YADE=1?"; exit 1; }
ls spheres/spheres_*.vtp > /dev/null 2>&1 || { echo "ERROR: no sphere VTK files written"; exit 1; }
ls springs/springs_*.vtp > /dev/null 2>&1 || { echo "ERROR: no spring VTK files written"; exit 1; }
echo "  DEM output check passed"

[ -f dtInfo.txt ] \
    || { echo "ERROR: dtInfo.txt not written — did the run reach iter 1?"; exit 1; }
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
then exit 1; fi
