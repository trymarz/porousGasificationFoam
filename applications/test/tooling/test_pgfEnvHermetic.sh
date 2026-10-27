#!/bin/bash
# pgf-env from a polluted caller (set -u, WM_PROJECT_USER_DIR elsewhere, a stale
# user dir with its own porousGasificationFoam on PATH) still prints its banner
# and resolves the solver inside this tree's prefix.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

isolated_build >"$TEST_TMP/build.log" 2>&1 || { cat "$TEST_TMP/build.log"; die "build.sh --isolated failed"; }
prefix="$(tree_prefix)" || die "cannot resolve the prefix"

stale="$TEST_TMP/stale-user-dir"
mkdir -p "$stale/bin" "$stale/lib"
printf '#!/bin/sh\necho stale solver\n' >"$stale/bin/porousGasificationFoam"
chmod +x "$stale/bin/porousGasificationFoam"

probe='command -v porousGasificationFoam; echo "PATH=$PATH"; echo "LD=$LD_LIBRARY_PATH"; echo "USERDIR=$WM_PROJECT_USER_DIR"'
bash -c '
    set -u
    export WM_PROJECT_USER_DIR="$1/elsewhere" FOAM_USER_APPBIN="$1/bin" FOAM_USER_LIBBIN="$1/lib"
    export PATH="$1/bin:$PATH" LD_LIBRARY_PATH="$1/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    command -v porousGasificationFoam | grep -q "^$1/bin/" || { echo "stale solver not first on PATH"; exit 3; }
    "$2" -- bash -c "$3"
' _ "$stale" "$PGF_ENV" "$probe" >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?

check "pgf-env exits 0 from a polluted set -u caller (rc=$rc)" [ "$rc" -eq 0 ]
check "banner names the prefix" grep -q "^pgf-env: prefix $prefix " "$TEST_TMP/err"
check "solver resolves inside $prefix/bin" grep -qx "$prefix/bin/porousGasificationFoam" "$TEST_TMP/out"
check "stale user dir dropped from PATH and LD_LIBRARY_PATH" bash -c '! grep -q "$1" "$2"' _ "$stale" "$TEST_TMP/out"
check "WM_PROJECT_USER_DIR is the prefix" grep -qx "USERDIR=$prefix" "$TEST_TMP/out"

# The prefix may itself sit under the default user dir (the default build root).
default_entries() {
    grep -E '^(PATH|LD)=' "$TEST_TMP/out" | cut -d= -f2- | tr ':' '\n' |
        grep -E "^$default(/|\$)" | grep -vE "^$prefix(/|\$)"
}
default="$(default_user_dir)"
[ -n "$default" ] || die "cannot determine the default user dir"
check "no PATH/LD_LIBRARY_PATH entry under the default user dir $default" \
    bash -c '[ -z "$1" ]' _ "$(default_entries)"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- stdout"; cat "$TEST_TMP/out"; echo "--- stderr"; cat "$TEST_TMP/err"; }
finish
