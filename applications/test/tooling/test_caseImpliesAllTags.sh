#!/bin/bash
# --case names one case whatever its tier: --list shows a convergence case
# without --tag, and running it gets past selection. PGF_TOOLING_RUN_CASES=1
# runs it for real (serial_m2, about 80 s) and expects PASS.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

case_rel="tutorials/cases/charOnlyMoveCases/serial_m2"
"$REGRESSION_DIR/Allrun" --list --case "$case_rel" >"$TEST_TMP/list" 2>&1
rc=$?
check "--list --case exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "--list --case lists $case_rel" grep -q "convergence *$case_rel\$" "$TEST_TMP/list"

"$REGRESSION_DIR/Allrun" --list --case charOnlyMoveCases/serial_m2 >"$TEST_TMP/list2" 2>&1
check "a trailing part of the path selects it too" grep -q "$case_rel\$" "$TEST_TMP/list2"

"$REGRESSION_DIR/Allrun" --list --case "$case_rel" --tag fast >"$TEST_TMP/list3" 2>&1
rc=$?
check "an explicit --tag still filters (rc=$rc, expect 2)" [ "$rc" -eq 2 ]

if [ "${PGF_TOOLING_RUN_CASES:-0}" = "1" ]; then
    ensure_prefix
    "$PGF_ENV" -- "$REGRESSION_DIR/Allrun" --case "$case_rel" --run-root "$TEST_TMP/runs" >"$TEST_TMP/run" 2>&1
    rc=$?
    check "running $case_rel without --tag passes (rc=$rc)" [ "$rc" -eq 0 ]
    [ "$rc" -eq 0 ] || tail -40 "$TEST_TMP/run"
else
    "$REGRESSION_DIR/Allrun" --case "$case_rel" --no-run --run-root "$TEST_TMP/runs" >"$TEST_TMP/run" 2>&1
    check "running without --tag gets past selection" grep -q "case: serial_m2" "$TEST_TMP/run"
fi
check "no 'matches tag' selection error" bash -c '! grep -q "matches tag" "$1"' _ "$TEST_TMP/run"
finish
