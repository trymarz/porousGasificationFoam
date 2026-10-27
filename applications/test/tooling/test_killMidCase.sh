#!/bin/bash
# Killing the runner while a case's solver is running changes nothing outside
# the run root. The runner gets its own session (setsid) and is killed by
# process group, so the kill cannot reach this script.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

ensure_prefix
touch "$TEST_TMP/stamp"
tutorials_state >"$TEST_TMP/before"
runs="$TEST_TMP/runs"

setsid "$PGF_ENV" -- "$REGRESSION_DIR/Allrun" --case tutorials/cases/charOnlyMoveCases/serial_m2 \
    --run-root "$runs" >"$TEST_TMP/run.log" 2>&1 &
pid=$!

for _ in $(seq 1 120); do
    find "$runs" -name 'log.porousGasificationFoam' -size +1k 2>/dev/null | grep -q . && break
    sleep 1
done
log="$(find "$runs" -name 'log.porousGasificationFoam' 2>/dev/null | head -1)"
check "the solver was running when killed" [ -n "$log" ]

kill -TERM -- "-$pid" 2>/dev/null
wait "$pid"
rc=$?
check "runner died on the signal (rc=$rc)" [ "$rc" -ne 0 ]
sleep 1
check "nothing left running in the killed group" bash -c '! kill -0 -- "-$1" 2>/dev/null' _ "$pid"

tutorials_state >"$TEST_TMP/after"
check "source tutorials/ unchanged" cmp -s "$TEST_TMP/before" "$TEST_TMP/after"
check "no source file newer than the start of the test" \
    bash -c '[ -z "$(find "$1/tutorials" "$1/applications" -newer "$2" -print -quit)" ]' _ "$PGF_ROOT" "$TEST_TMP/stamp"
check "the partial run is in the run root" bash -c '[ -n "$(find "$1" -name "log.*" -print -quit)" ]' _ "$runs"
echo "  (this script is still running after the kill: the kill did not reach its caller)"
finish
