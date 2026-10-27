#!/bin/bash
# build.sh --isolated twice: the second run compiles nothing. A changed .C file
# gives a new key, and building it leaves the old prefix byte-identical. The
# change is an untracked probe file that no Make/files compiles, removed on exit.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

isolated_build >"$TEST_TMP/first.log" 2>&1 || { cat "$TEST_TMP/first.log"; die "first build failed"; }
old="$(tree_prefix)" || die "cannot resolve the prefix"
tree_sums "$old" >"$TEST_TMP/old.before"

isolated_build >"$TEST_TMP/second.log" 2>&1
rc=$?
check "second build exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "second build reports a cache hit" grep -q "Cache hit" "$TEST_TMP/second.log"
check "second build runs no wmake target" bash -c '! grep -q "Building " "$1"' _ "$TEST_TMP/second.log"

probe="$PGF_ROOT/utilities/setPorosity/pgfToolingProbe.C"
[ -e "$probe" ] && die "$probe already exists"
trap 'rm -f "$probe"; rm -rf "$TEST_TMP"' EXIT
echo "// tooling test probe: changes the source fingerprint only" >"$probe"

new="$(tree_prefix)" || die "cannot resolve the probed prefix"
check "a changed .C file gives a new key (${new##*/})" [ "$new" != "$old" ]

isolated_build >"$TEST_TMP/third.log" 2>&1
rc=$?
check "build for the new key exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "new prefix is complete" [ -f "$new/.pgf-build.json" ]
check "new prefix has its own solver" [ -x "$new/bin/porousGasificationFoam" ]

tree_sums "$old" >"$TEST_TMP/old.after"
check "old prefix files are byte-identical" cmp -s "$TEST_TMP/old.before" "$TEST_TMP/old.after"

rm -f "$probe"
case "$new" in
"$PGF_BUILD_ROOT"/*) rm -rf "$new" "$new.lock" ;;
esac
check "probe removed; key is back to ${old##*/}" [ "$(tree_prefix)" = "$old" ]
finish
