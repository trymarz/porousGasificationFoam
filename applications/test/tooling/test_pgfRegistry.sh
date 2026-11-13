#!/bin/bash
# A finished isolated build records who built it (.pgf-build.json schema 2) and
# registers this checkout; pgf status --json and pgf ls --json report it.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

isolated_build >"$TEST_TMP/build.log" 2>&1 || { cat "$TEST_TMP/build.log"; die "build.sh --isolated failed"; }
prefix="$(tree_prefix)" || die "cannot resolve the prefix"
key="${prefix##*/}"

field() {
    python3 -c 'import json, sys
d = json.load(open(sys.argv[1]))
for k in sys.argv[2].split("."):
    d = d[k] if isinstance(d, dict) else d[int(k)]
print(d)' "$@"
}

m="$prefix/.pgf-build.json"
check "manifest schema 2" [ "$(field "$m" schema)" = 2 ]
check "manifest names this checkout" [ "$(field "$m" checkout.path)" = "$PGF_ROOT" ]
check "manifest names the worktree" [ "$(field "$m" checkout.worktree)" = "${PGF_ROOT##*/}" ]
check "manifest records dirty state" bash -c 'case "$1" in True|False|None) ;; *) exit 1 ;; esac' _ "$(field "$m" checkout.dirty)"

ws="$(grep -l "\"path\": \"$PGF_ROOT\"" "$PGF_BUILD_ROOT"/.pgf/workspaces/*.json 2>/dev/null | head -1)"
check "workspace file registered" [ -n "$ws" ]
[ -z "$ws" ] || check "workspace records the yade0 key" [ "$(field "$ws" builds.yade0)" = "$key" ]

"$PGF" status --json >"$TEST_TMP/status.json" 2>"$TEST_TMP/status.err"
check "status --json exits 0" [ $? -eq 0 ]
check "status --json: yade0 key and complete" [ "$(field "$TEST_TMP/status.json" builds.yade0.key)/$(field "$TEST_TMP/status.json" builds.yade0.complete)" = "$key/True" ]
check "status --json: settings layer" [ "$(field "$TEST_TMP/status.json" settings.PGF_BUILD_ROOT.layer)" = env ]
check "status PATH --json answers for that checkout" \
    bash -c '"$1" status "$2" --json | grep -q "\"checkout\": \"$2\""' _ "$PGF" "$PGF_ROOT"

"$PGF" ls --json >"$TEST_TMP/ls.json" 2>"$TEST_TMP/ls.err"
check "ls --json exits 0" [ $? -eq 0 ]
check "ls --json lists the build as current and used by this checkout" python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
b = [b for b in d["builds"] if b["key"] == sys.argv[2]]
sys.exit(0 if b and b[0]["current"] and b[0]["complete"] and sys.argv[3] in b[0]["used_by"] else 1)' \
    "$TEST_TMP/ls.json" "$key" "$PGF_ROOT"
check "ls marks the current build" bash -c '"$1" ls | grep -q "^\* $2 "' _ "$PGF" "$key"
check "ls rejects unknown options" bash -c '! "$1" ls --bogus 2>/dev/null' _ "$PGF"

[ "$TEST_FAILURES" -eq 0 ] || { cat "$TEST_TMP/status.err" "$TEST_TMP/ls.err"; }
finish
