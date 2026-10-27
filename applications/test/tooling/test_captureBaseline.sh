#!/bin/bash
# captureBaseline.sh on a run where only TsAverage and TsMax fail copies only
# those two files. The run is real (charOnlyMoveCases/serial in copy mode); the
# two files are then perturbed in the run copy, and the baseline written is a
# scratch copy of the case, so the committed reference/ is never touched.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

ensure_prefix
case_rel="tutorials/cases/charOnlyMoveCases/serial"
"$PGF_ENV" -- "$REGRESSION_DIR/Allrun" --case "$case_rel" --run-root "$TEST_TMP/runs" --keep-runs \
    >"$TEST_TMP/run.log" 2>&1
rc=$?
check "the run passes before perturbing (rc=$rc)" [ "$rc" -eq 0 ]
run="$TEST_TMP/runs/${case_rel//\//__}/$case_rel"
[ -d "$run/postProcessing" ] || { tail -30 "$TEST_TMP/run.log"; die "no run output at $run"; }

for f in regressionTsAverage regressionTsMax; do
    awk '/^#/ || NF == 0 {print; next} {printf "%s", $1; for (i = 2; i <= NF; i++) printf "\t%.10e", $i * 1.01; print ""}' \
        "$run/postProcessing/$f/0/volFieldValue.dat" >"$TEST_TMP/p" && mv "$TEST_TMP/p" "$run/postProcessing/$f/0/volFieldValue.dat"
done

scratch="$TEST_TMP/case"
cp -a "$PGF_ROOT/$case_rel" "$scratch"
tree_sums "$scratch/reference" >"$TEST_TMP/ref.before"

"$REGRESSION_DIR/tools/captureBaseline.sh" "$scratch" --run-dir "$run" >"$TEST_TMP/capture.log" 2>&1
rc=$?
check "captureBaseline exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "it prints a before/after table" grep -qE "^ +first +1 " "$TEST_TMP/capture.log"
tree_sums "$scratch/reference" >"$TEST_TMP/ref.after"

changed="$(diff "$TEST_TMP/ref.before" "$TEST_TMP/ref.after" | grep '^>' | sed -E 's#.*/reference/postProcessing/##' | sort | tr '\n' ' ')"
check "exactly TsAverage and TsMax changed (got: ${changed:-none})" \
    [ "$changed" = "regressionTsAverage/0/volFieldValue.dat regressionTsMax/0/volFieldValue.dat " ]
check "the copied files equal the run's" \
    cmp -s "$scratch/reference/postProcessing/regressionTsMax/0/volFieldValue.dat" "$run/postProcessing/regressionTsMax/0/volFieldValue.dat"
check "the committed baseline is untouched" \
    bash -c '[ -z "$(git -C "$1" status --porcelain -- "$2")" ]' _ "$PGF_ROOT" "$case_rel"
[ "$TEST_FAILURES" -eq 0 ] || cat "$TEST_TMP/capture.log"
finish
