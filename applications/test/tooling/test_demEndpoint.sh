#!/bin/bash
# The Yade endpoint follows the solver's controlDict (#60), without running
# either: every listed DEM case derives its step count, the helper refuses what
# it cannot derive, and --short-steps' edit of a staged copy is what Yade then
# follows, leaving the source case untouched.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
. "$REGRESSION_DIR/tools/regressionLib.sh"

HELPER_DIR="$PGF_ROOT/tutorials/cases/DEM_common"
SHORTEN="$REGRESSION_DIR/tools/shortenControlDict.py"

# steps <caseDir> <yadeDt> — the helper's NSTEPS, or nothing when it refuses.
steps() {
    python3 -I -B - "$HELPER_DIR" "$1" "$2" <<'PY' 2>/dev/null
import sys
sys.path.insert(0, sys.argv[1])
import yadeEndpoint
try:
    print(yadeEndpoint.yadeSteps(float(sys.argv[3]), caseDir=sys.argv[2], log=lambda m: None))
except yadeEndpoint.YadeEndpointError:
    pass
PY
}

reg_load_cases "$REGRESSION_DIR/cases_yade.list" DEM_CASES || die "cannot read cases_yade.list"
for c in "${DEM_CASES[@]}"; do
    check "$c: 2000 solver steps at Yade dt 5e-6 give NSTEPS 2000" \
        [ "$(steps "$PGF_ROOT/$c" 5e-6)" = "2000" ]
done
check "C++ truncation: simple_line_case (0.005/5e-6 = 999.99...) gives 24000 x 999" \
    [ "$(steps "$PGF_ROOT/tutorials/experimental/simple_line_case" 5e-6)" = "23976000" ]
check "Yade dt 3e-7 under deltaT 1e-6 exchanges every 3 steps" \
    [ "$(steps "$PGF_ROOT/tutorials/cases/MicroTGA-DEM_lambdaDotOff" 3e-7)" = "6000" ]

# fake_case <controlDict body> — a case dir holding only that controlDict.
fake_case() {
    local d
    d="$(mktemp -d "$TEST_TMP/case-XXXXXX")"
    mkdir "$d/system"
    printf 'FoamFile { version 2.0; object controlDict; }\n%s\n' "$1" >"$d/system/controlDict"
    printf '%s' "$d"
}
check "refuses adjustTimeStep yes" \
    [ -z "$(steps "$(fake_case 'endTime 1; deltaT 0.1; adjustTimeStep yes;')" 0.01)" ]
check "refuses stopAt writeNow" \
    [ -z "$(steps "$(fake_case 'endTime 1; deltaT 0.1; stopAt writeNow;')" 0.01)" ]
check "refuses a non-numeric endTime" \
    [ -z "$(steps "$(fake_case 'endTime $end; deltaT 0.1;')" 0.01)" ]
check "a non-integer endTime/deltaT counts the solver's steps (10.3 -> 10)" \
    [ "$(steps "$(fake_case 'endTime 0.0103; deltaT 1e-3;')" 0.01)" = "10" ]

before="$(git -C "$PGF_ROOT" diff -- tutorials | sha1sum)"
dem="tutorials/cases/MicroTGA-DEM_lambdaDotOff"
staged="$(reg_stage_case "$PGF_ROOT" "$dem" "$TEST_TMP/dem")" || die "cannot stage $dem"
"$PGF" exec --bare -- python3 "$SHORTEN" "$staged/system/controlDict" 200 \
    >"$TEST_TMP/shorten.log" 2>&1
rc=$?
check "shortenControlDict.py edits the copy (rc=$rc)" [ "$rc" -eq 0 ]
check "the shortened copy derives NSTEPS 200" [ "$(steps "$staged" 5e-6)" = "200" ]
check "the copy writes every 100 steps" \
    grep -qE '^writeInterval +0\.0001;' "$staged/system/controlDict"
check "the source case still derives NSTEPS 2000" [ "$(steps "$PGF_ROOT/$dem" 5e-6)" = "2000" ]
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
