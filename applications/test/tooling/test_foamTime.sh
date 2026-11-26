#!/bin/bash
# The Yade scripts follow the solver's controlDict through DEM_common/foamTime.py
# (#328), without running either: every listed DEM case reads its times, write
# intervals convert to seconds, and --short-steps' edit of a staged copy is what
# Yade then follows, leaving the source case untouched.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
. "$REGRESSION_DIR/tools/regressionLib.sh"

HELPER_DIR="$PGF_ROOT/tutorials/cases/DEM_common"
SHORTEN="$REGRESSION_DIR/tools/shortenControlDict.py"

# times <caseDir> — "deltaT writeInterval endTime", or nothing when it cannot read them.
times() {
    python3 -I -B - "$HELPER_DIR" "$1" <<'PY' 2>/dev/null
import sys
sys.path.insert(0, sys.argv[1])
import foamTime
try:
    t = foamTime.read(sys.argv[2])
    print(f"{t.deltaT:g} {t.writeInterval:g} {t.endTime:g}")
except ValueError:
    pass
PY
}

reg_load_cases "$REGRESSION_DIR/cases_yade.list" DEM_CASES || die "cannot read cases_yade.list"
for c in "${DEM_CASES[@]}"; do
    check "$c: deltaT 1e-6, writes every 2e-4 s, ends at 0.002" \
        [ "$(times "$PGF_ROOT/$c")" = "1e-06 0.0002 0.002" ]
done

# fake_case <controlDict body> — a case dir holding only that controlDict.
fake_case() {
    local d
    d="$(mktemp -d "$TEST_TMP/case-XXXXXX")"
    mkdir "$d/system"
    printf 'FoamFile { version 2.0; object controlDict; }\n%s\n' "$1" >"$d/system/controlDict"
    printf '%s' "$d"
}
check "writeControl timeStep counts solver steps (5 x 0.1 s)" \
    [ "$(times "$(fake_case 'endTime 1; deltaT 0.1; writeControl timeStep; writeInterval 5;')")" = "0.1 0.5 1" ]
check "refuses writeControl cpuTime" \
    [ -z "$(times "$(fake_case 'endTime 1; deltaT 0.1; writeControl cpuTime; writeInterval 5;')")" ]
check "refuses a non-numeric deltaT" \
    [ -z "$(times "$(fake_case 'endTime 1; deltaT $dt; writeInterval 1;')")" ]

before="$(git -C "$PGF_ROOT" diff -- tutorials | sha1sum)"
dem="tutorials/cases/MicroTGA-DEM_lambdaDotOff"
staged="$(reg_stage_case "$PGF_ROOT" "$dem" "$TEST_TMP/dem")" || die "cannot stage $dem"
"$PGF" exec --bare -- python3 "$SHORTEN" "$staged/system/controlDict" 200 \
    >"$TEST_TMP/shorten.log" 2>&1
rc=$?
check "shortenControlDict.py edits the copy (rc=$rc)" [ "$rc" -eq 0 ]
check "the shortened copy ends after 200 steps and writes every 100" \
    [ "$(times "$staged")" = "1e-06 0.0001 0.0002" ]
check "the source case is unchanged" [ "$(times "$PGF_ROOT/$dem")" = "1e-06 0.0002 0.002" ]
check "source tutorials/ unchanged" [ "$before" = "$(git -C "$PGF_ROOT" diff -- tutorials | sha1sum)" ]

"$REGRESSION_DIR/tools/runDEMCase.sh" "$PGF_ROOT/$dem" --short-steps 200 \
    >"$TEST_TMP/no-copy.log" 2>&1
rc=$?
check "runDEMCase.sh refuses --short-steps without --run-dir (rc=$rc)" [ "$rc" -eq 2 ]
"$REGRESSION_DIR/tools/runDEMCase.sh" "$PGF_ROOT/$dem" --nsteps 200 \
    >"$TEST_TMP/nsteps.log" 2>&1
rc=$?
check "runDEMCase.sh rejects the removed --nsteps (rc=$rc)" [ "$rc" -eq 2 ]

[ "$TEST_FAILURES" -eq 0 ] || cat "$TEST_TMP/shorten.log"
finish
