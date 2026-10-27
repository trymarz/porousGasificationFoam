#!/bin/bash
# With PGF_ISOLATED_ONLY=1, build.sh without --prefix / --isolated exits
# non-zero and writes nothing: not the user output dirs (here an empty scratch
# pair) and not the checkout's generated lnInclude/ and Make/<WM_OPTIONS>/.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

user="$TEST_TMP/user"
mkdir -p "$user/bin" "$user/lib"
snapshot() {
    git -C "$PGF_ROOT" status --porcelain --ignored
    find "$PGF_ROOT" -path '*/Make/*' -newer "$TEST_TMP/stamp" -print 2>/dev/null
}
touch "$TEST_TMP/stamp"
snapshot >"$TEST_TMP/tree.before"

run_shared() {
    bash -c '
        cmd=("$@"); set +eu; set --; . "$PGF_FOAM_BASHRC"
        export FOAM_USER_APPBIN="$0/bin" FOAM_USER_LIBBIN="$0/lib" PGF_ISOLATED_ONLY=1
        "${cmd[@]}"
    ' "$user" "$@"
}

for args in "build" "build --all" "clean --all --purge"; do
    # shellcheck disable=SC2086
    run_shared "$BUILD_SH" $args >"$TEST_TMP/out" 2>&1
    rc=$?
    check "build.sh $args refused (rc=$rc)" [ "$rc" -ne 0 ]
    check "build.sh $args names PGF_ISOLATED_ONLY" grep -q "PGF_ISOLATED_ONLY=1" "$TEST_TMP/out"
    check "build.sh $args ran no wmake target" bash -c '! grep -qE "Building |Cleaning " "$1"' _ "$TEST_TMP/out"
done

check "user bin/ and lib/ still empty" bash -c '[ -z "$(find "$1" -mindepth 2)" ]' _ "$user"
snapshot >"$TEST_TMP/tree.after"
check "checkout unchanged" cmp -s "$TEST_TMP/tree.before" "$TEST_TMP/tree.after"
finish
