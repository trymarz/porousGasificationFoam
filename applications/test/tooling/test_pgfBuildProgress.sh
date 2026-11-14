#!/bin/bash
# A prefix build shows one [n/N] line per target, logs wmake to <prefix>/build.log
# and, on failure, prints that target's log tail (#280). A fake wmake; nothing compiles.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

mkdir -p "$TEST_TMP/bin"
cat >"$TEST_TMP/bin/wmake" <<'EOF'
#!/bin/sh
echo "g++ -O3 a-very-long-compile-line $PWD"
case "$PWD" in *"${FAIL_IN:-none}"*) echo "error: boom in $PWD"; exit 1 ;; esac
EOF
chmod +x "$TEST_TMP/bin/wmake"

# fb ARGS — build.sh in a fake OpenFOAM env with the fake wmake.
fb() {
    env -i PATH="$TEST_TMP/bin:/usr/bin:/bin" HOME="$TEST_TMP" TMPDIR="$TEST_TMP" FAIL_IN="${FAIL_IN:-none}" \
        WM_PROJECT_DIR=/fake WM_OPTIONS=linux64 WM_PROJECT_VERSION=v2406 \
        FOAM_USER_LIBBIN="$TEST_TMP/u/lib" FOAM_USER_APPBIN="$TEST_TMP/u/bin" \
        "$BUILD_SH" --prefix "$TEST_TMP/pre" "$@"
}
targets=(--libs-only --no-DEM --no-thermophysicalModels)

fb "${targets[@]}" >"$TEST_TMP/out" 2>&1
rc=$?
check "a prefix build exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "  shows one [n/N] line per target, no separate Building line off a terminal" \
    bash -c 'grep -q "\[1/3\] fieldPorosityModel done (" "$1" && grep -q "\[3/3\] pyrolysisModels done (" "$1" &&
        ! grep -q "Building" "$1"' _ "$TEST_TMP/out"
check "  and labels the prefix it prints last" grep -qx "prefix: $TEST_TMP/pre" "$TEST_TMP/out"
check "  keeps wmake's output off the terminal" bash -c '! grep -q "a-very-long-compile-line" "$1"' _ "$TEST_TMP/out"
check "  and logs it to <prefix>/build.log" grep -q "a-very-long-compile-line" "$TEST_TMP/pre/build.log"
check "  and ends with the time and the log path" \
    grep -q "build complete in [0-9]*s; wmake log: $TEST_TMP/pre/build.log" "$TEST_TMP/out"

FAIL_IN=pyrolysisModels fb "${targets[@]}" >"$TEST_TMP/out" 2>&1
rc=$?
check "a failing target fails the build (rc=$rc)" [ "$rc" -eq 1 ]
check "  and shows its log tail and the log path" \
    bash -c 'grep -q "^    error: boom in .*pyrolysisModels" "$1" && grep -q "full log: .*/pre/build.log" "$1"' _ "$TEST_TMP/out"

fb "${targets[@]}" -v >"$TEST_TMP/out" 2>&1
check "--verbose streams wmake's output" grep -q "a-very-long-compile-line" "$TEST_TMP/out"

finish
