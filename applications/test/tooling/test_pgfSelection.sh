#!/bin/bash
# Build selection (#264): --use > terminal (PGF_BUILD, pgf use) > auto; a stale
# selection warns in exec and is refused by run; runs hold <key>.use.lock, which
# pgf gc respects; gc without --yes and without a terminal removes nothing.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

isolated_build >"$TEST_TMP/build.log" 2>&1 || { cat "$TEST_TMP/build.log"; die "build.sh --isolated failed"; }
auto="$(tree_prefix)" || die "cannot resolve the prefix"

# A second, older build: a copy of this one under another key.
other="000000000000-yade0-${auto##*-yade0-}"
cp -a "$auto" "$PGF_BUILD_ROOT/$other" || die "cannot copy the prefix"
trap 'rm -rf "$TEST_TMP" "$PGF_BUILD_ROOT/$other" "$PGF_BUILD_ROOT/$other".*lock' EXIT
python3 - "$PGF_BUILD_ROOT/$other/.pgf-build.json" "$other" <<'PY'
import json, sys
m = json.load(open(sys.argv[1])); m["key"] = sys.argv[2]; m["git_head"] = "0123456789abcdef" * 2
json.dump(m, open(sys.argv[1], "w"))
PY
touch -d '2 days ago' "$PGF_BUILD_ROOT/$other"

wm_dir() { "$PGF" exec "$@" -- printenv WM_PROJECT_USER_DIR 2>"$TEST_TMP/err"; }

check "auto selects this checkout's build" [ "$(wm_dir)" = "$auto" ]
check "--use KEY-PREFIX selects the other build" [ "$(wm_dir --use 00000000)" = "$PGF_BUILD_ROOT/$other" ]
check "a stale selection warns in exec" grep -q "warning: the selected build $other (cli) is not" "$TEST_TMP/err"
check "--use COMMIT selects the build from that commit" [ "$(wm_dir --use 0123456789)" = "$PGF_BUILD_ROOT/$other" ]
check "--use PATH selects that prefix" [ "$(wm_dir --use "$PGF_BUILD_ROOT/$other")" = "$PGF_BUILD_ROOT/$other" ]
check "--use WORKTREE selects its last build" [ "$(wm_dir --use "${PGF_ROOT##*/}")" = "$auto" ]
check "PGF_BUILD in the environment is the terminal layer" \
    bash -c '[ "$(PGF_BUILD=$1 "$2" exec -- printenv WM_PROJECT_USER_DIR 2>"$3")" = "$4" ] && grep -q "(terminal)" "$3"' \
    _ "$other" "$PGF" "$TEST_TMP/err" "$PGF_BUILD_ROOT/$other"
check "--use auto beats the terminal layer" \
    bash -c '[ "$(PGF_BUILD=$1 "$2" exec --use auto -- printenv WM_PROJECT_USER_DIR 2>/dev/null)" = "$3" ]' _ "$other" "$PGF" "$auto"
check "an unknown selector exits 2" bash -c '"$1" exec --use nosuchbuild -- true 2>/dev/null; [ $? -eq 2 ]' _ "$PGF"

check "pgf use prints an export line" [ "$("$PGF" use 00000000 2>/dev/null)" = "export PGF_BUILD=$other" ]
check "pgf use auto unsets it" [ "$("$PGF" use auto 2>/dev/null)" = "unset PGF_BUILD" ]
out="$(printf 'pgf use 00000000 2>/dev/null\nprintenv PGF_BUILD\nexit\n' | "$PGF" shell 2>/dev/null)"
check "pgf shell's pgf function applies pgf use" grep -qx "$other" <<<"$out"

case_dir="$TEST_TMP/case"
mkdir -p "$case_dir" && printf '#!/bin/sh\necho ran\n' >"$case_dir/Allrun" && chmod +x "$case_dir/Allrun"
"$PGF" run --use 00000000 "$case_dir" >"$TEST_TMP/out" 2>"$TEST_TMP/err"
check "run refuses a stale build (exit 2, script not run)" \
    bash -c '[ "$1" -eq 2 ] && ! grep -q ran "$2"' _ $? "$TEST_TMP/out"
check "run --allow-stale runs it" bash -c '"$1" run --use 00000000 --allow-stale "$2" 2>/dev/null | grep -qx ran' _ "$PGF" "$case_dir"
check "run --build runs on this checkout's build" bash -c '"$1" run --build "$2" 2>/dev/null | grep -qx ran' _ "$PGF" "$case_dir"
check "run --build with a selected build exits 2" bash -c '"$1" run --build --use 00000000 "$2" >/dev/null 2>&1; [ $? -eq 2 ]' _ "$PGF" "$case_dir"

check "builds --json lists both" python3 -c '
import json, subprocess, sys
d = json.loads(subprocess.check_output([sys.argv[1], "builds", "--json"], stderr=subprocess.DEVNULL))
keys = {b["key"] for b in d["builds"]}
sys.exit(0 if {sys.argv[2], sys.argv[3]} <= keys and "workspaces" not in d else 1)' "$PGF" "$other" "${auto##*/}"

# gc: while a run holds the other build it stays; afterwards --keep 0 --yes removes it.
"$PGF" exec --use 00000000 -- sleep 30 2>/dev/null &
holder=$!
for _ in $(seq 50); do "$PGF" builds 2>/dev/null | grep -q "$other.*\[in use\]" && break; sleep 0.2; done
check "builds marks a running build in use" bash -c '"$1" builds 2>/dev/null | grep -q "$2.*\[in use\]"' _ "$PGF" "$other"
"$PGF" gc --keep 0 --yes >"$TEST_TMP/gc" 2>&1
check "gc keeps a build in use" bash -c '[ -d "$1" ] && grep -q "keep   $2 (in use)" "$3"' _ "$PGF_BUILD_ROOT/$other" "$other" "$TEST_TMP/gc"
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
for _ in $(seq 50); do "$PGF" builds 2>/dev/null | grep -q "$other.*\[in use\]" || break; sleep 0.2; done

"$PGF" gc --keep 0 </dev/null >/dev/null 2>"$TEST_TMP/err"
check "gc without --yes and without a terminal exits 2 and keeps it" \
    bash -c '[ "$1" -eq 2 ] && [ -d "$2" ]' _ $? "$PGF_BUILD_ROOT/$other"
check "gc --dry-run lists it for removal" bash -c '"$1" gc --keep 0 --dry-run 2>/dev/null | grep -q "^remove $2"' _ "$PGF" "$other"
"$PGF" gc --keep 0 --yes >"$TEST_TMP/gc" 2>&1
check "gc --keep 0 --yes removes the unused build" [ ! -e "$PGF_BUILD_ROOT/$other" ]
check "gc keeps this checkout's build" [ -f "$auto/.pgf-build.json" ]

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; echo "--- gc"; cat "$TEST_TMP/gc"; }
finish
