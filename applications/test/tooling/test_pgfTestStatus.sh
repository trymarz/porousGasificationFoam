#!/bin/bash
# pgf-test records last/<tier>.json, --if-stale skips a fresh PASS, and after a
# source change --status reports the tier stale. The tier is a one-case smoke
# tier from a scratch tiers.conf; the change is an untracked probe .C file.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

PGF_TEST="$PGF_ROOT/pgf-test"
export PGF_RUN_ROOT="$TEST_TMP/runs"
export PGF_TIERS_CONF="$TEST_TMP/tiers.conf"
echo 'pgf_tier smoke Allrun yade0 1 - - "--case tutorials/cases/canonical/darcy" "test"' >"$PGF_TIERS_CONF"
record="$PGF_RUN_ROOT/last/smoke.json"
field() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$record" "$1"; }

"$PGF_TEST" smoke >"$TEST_TMP/run.log" 2>&1
rc=$?
check "pgf-test smoke exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "it records last/smoke.json" [ -f "$record" ]
key="$(tree_prefix)"
key="${key##*/}"
check "the record has outcome PASS" [ "$(field outcome)" = "PASS" ]
check "the record names this tree's key" [ "$(field key)" = "$key" ]
check "the record points at the run's result.json" [ -f "$(field result_json)" ]

"$PGF_TEST" --status smoke >"$TEST_TMP/status1" 2>&1
check "--status reports the tier current" grep -qE '^smoke +PASS .* current$' "$TEST_TMP/status1"
"$PGF_TEST" smoke --if-stale 1d >"$TEST_TMP/skip" 2>&1
check "--if-stale 1d skips a fresh PASS" grep -q "is fresh" "$TEST_TMP/skip"
"$PGF_TEST" --status --if-stale 0s smoke >"$TEST_TMP/status0" 2>&1
check "--status --if-stale 0s reports it stale by age" grep -q "stale (older than 0s)" "$TEST_TMP/status0"

probe="$PGF_ROOT/utilities/setPorosity/pgfToolingProbe.C"
[ -e "$probe" ] && die "$probe already exists"
trap 'rm -f "$probe"; rm -rf "$TEST_TMP"' EXIT
echo "// tooling test probe: changes the source fingerprint only" >"$probe"
"$PGF_TEST" --status smoke >"$TEST_TMP/status2" 2>&1
check "after a source change --status reports the tier stale" grep -qE '^smoke +PASS .*stale \(tree is now ' "$TEST_TMP/status2"
rm -f "$probe"
"$PGF_TEST" --status smoke >"$TEST_TMP/status3" 2>&1
check "with the change undone it is current again" grep -qE ' current$' "$TEST_TMP/status3"
[ "$TEST_FAILURES" -eq 0 ] || { cat "$TEST_TMP"/status*; tail -20 "$TEST_TMP/run.log"; }
finish
