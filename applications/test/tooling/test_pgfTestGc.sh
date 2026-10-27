#!/bin/bash
# pgf-test --gc keeps this tree's prefix even when it is the oldest, keeps a
# prefix whose build lock is held, keeps the newest M others and every run dir a
# last record points at. Everything lives in scratch build and run roots.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

PGF_TEST="$PGF_ROOT/pgf-test"
export PGF_BUILD_ROOT="$TEST_TMP/builds" PGF_RUN_ROOT="$TEST_TMP/runs"
mkdir -p "$PGF_BUILD_ROOT" "$PGF_RUN_ROOT/last"

current="$(tree_prefix)" || die "cannot resolve this tree's prefix"
current="${current##*/}"
fake() { # fake <name> <age-minutes>
    mkdir -p "$PGF_BUILD_ROOT/$1/bin" && echo '{}' >"$PGF_BUILD_ROOT/$1/.pgf-build.json"
    touch -d "-$2 minutes" "$PGF_BUILD_ROOT/$1"
}
fake "$current" 600
for i in 1 2 3 4 5; do fake "old${i}000000000-yade0-v0-gcc0" $((i * 10)); done
fake "locked00000000-yade0-v0-gcc0" 500
exec {held}>"$PGF_BUILD_ROOT/locked00000000-yade0-v0-gcc0.lock"
flock "$held"

for i in 1 2 3 4; do
    mkdir -p "$PGF_RUN_ROOT/pgf-test-fast-$i"
    touch -d "-$((i * 10)) minutes" "$PGF_RUN_ROOT/pgf-test-fast-$i"
done
printf '{"run_dir": "%s"}\n' "$PGF_RUN_ROOT/pgf-test-fast-4" >"$PGF_RUN_ROOT/last/fast.json"

"$PGF_TEST" --gc --keep-prefixes 2 --keep-runs 1 >"$TEST_TMP/gc.log" 2>&1
rc=$?
exists() { [ -d "$1" ]; }
gone() { [ ! -e "$1" ]; }
check "--gc exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "this tree's prefix kept although oldest" exists "$PGF_BUILD_ROOT/$current"
check "locked prefix kept" exists "$PGF_BUILD_ROOT/locked00000000-yade0-v0-gcc0"
check "two newest others kept" bash -c '[ -d "$1/old1000000000-yade0-v0-gcc0" ] && [ -d "$1/old2000000000-yade0-v0-gcc0" ]' _ "$PGF_BUILD_ROOT"
check "older others removed" bash -c '! ls -d "$1"/old[345]* 2>/dev/null | grep -q .' _ "$PGF_BUILD_ROOT"
check "newest run kept" exists "$PGF_RUN_ROOT/pgf-test-fast-1"
check "run named by a last record kept" exists "$PGF_RUN_ROOT/pgf-test-fast-4"
check "other runs removed" bash -c '[ ! -e "$1/pgf-test-fast-2" ] && [ ! -e "$1/pgf-test-fast-3" ]' _ "$PGF_RUN_ROOT"

exec {held}>&-
"$PGF_TEST" --gc --keep-prefixes 0 --keep-runs 0 --dry-run >"$TEST_TMP/dry.log" 2>&1
check "--dry-run removes nothing" exists "$PGF_BUILD_ROOT/old1000000000-yade0-v0-gcc0"
check "--dry-run never offers this tree's prefix" bash -c '! grep -q "would remove .*/$2" "$1"' _ "$TEST_TMP/dry.log" "$current"
[ "$TEST_FAILURES" -eq 0 ] || cat "$TEST_TMP/gc.log"
finish
