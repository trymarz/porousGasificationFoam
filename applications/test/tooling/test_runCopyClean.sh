#!/bin/bash
# A copy-mode run leaves the source tree as it was: a real run of a fast case,
# and a symlink-heavy DEM case staged and cleaned (Allclean) in its copy, whose
# links and ../../../applications path must resolve there.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
. "$REGRESSION_DIR/tools/regressionLib.sh"

ensure_prefix
tutorials_state >"$TEST_TMP/before"

"$PGF_ENV" -- "$REGRESSION_DIR/Allrun" --case tutorials/cases/canonical/darcy \
    --run-root "$TEST_TMP/runs" --keep-runs >"$TEST_TMP/fast.log" 2>&1
rc=$?
check "fast case canonical/darcy passes in copy mode (rc=$rc)" [ "$rc" -eq 0 ]
copy="$TEST_TMP/runs/tutorials__cases__canonical__darcy/tutorials/cases/canonical/darcy"
check "its output is in the copy" [ -d "$copy/postProcessing" ]

dem="tutorials/cases/MicroTGA-DEM_lambdaDotOff"
staged="$(reg_stage_case "$PGF_ROOT" "$dem" "$TEST_TMP/dem")"
check "DEM case staged as a real directory" bash -c '[ -d "$1" ] && [ ! -L "$1" ]' _ "$staged"
check "DEM copy has symlinks into the common case" bash -c '[ "$(find "$1" -type l | wc -l)" -gt 0 ]' _ "$staged"
check "every symlink in the DEM copy resolves" bash -c '[ -z "$(find "$1" -xtype l)" ]' _ "$staged"
check "../../../applications/.../checkFieldSign.py resolves from the copy" \
    [ -f "$staged/../../../applications/test/regression/tools/checkFieldSign.py" ]
"$PGF_ENV" -- bash -c 'cd "$1" && ./Allclean' _ "$staged" >"$TEST_TMP/dem-clean.log" 2>&1
rc=$?
check "DEM Allclean runs in the copy (rc=$rc)" [ "$rc" -eq 0 ]

tutorials_state >"$TEST_TMP/after"
check "source tutorials/ and applications/ unchanged (git status --ignored, git diff)" cmp -s "$TEST_TMP/before" "$TEST_TMP/after"
[ "$TEST_FAILURES" -eq 0 ] || { diff "$TEST_TMP/before" "$TEST_TMP/after"; tail -30 "$TEST_TMP/fast.log"; }
finish
