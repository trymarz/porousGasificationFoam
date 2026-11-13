#!/bin/bash
# Foam-Yade coupling checks (#257 PR C): a coupling stamp from other headers
# stops build.sh --yade before wmake and pgf --yade at setup, both naming
# the rebuild command; the yade1 build key follows the submodule pin and the
# coupling headers while the yade0 key does not; the compile-time guard trips
# only on a different FOAMYADE_COUPLING_API. Needs a real PGF_FOAM_BASHRC.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
. "$PGF_ROOT/utilities/pgf/coupling.sh"

# fake_trunk DIR TEXT — a Foam-Yade tree whose FoamYade.H holds TEXT.
fake_trunk() {
    local c="$1/pkg/openfoam/coupling/FoamYade"
    mkdir -p "$c/lnInclude" "$c/meshtree/lnInclude"
    printf '%s\n' "$2" >"$c/FoamYade.H"
}
trunk="$TEST_TMP/trunk"
fake_trunk "$trunk" "// api A"
libdir="$TEST_TMP/yadelibs"
mkdir -p "$libdir"
fake_trunk "$TEST_TMP/oldtrunk" "// api OLD"
pgf_coupling_write_stamp "$libdir" "$TEST_TMP/oldtrunk" || die "cannot write a stamp"

check "the stamp records the old trunk's header hash" \
    [ "$(pgf_coupling_stamp_field "$libdir" apiHash)" = "$(pgf_coupling_api_hash "$TEST_TMP/oldtrunk")" ]
check "check fails against other headers" bash -c '! "$@" >/dev/null' _ \
    bash -c '. "$1/utilities/pgf/coupling.sh"; PGF_ROOT="$1" pgf_coupling_check "$2" "$3"' _ "$PGF_ROOT" "$libdir" "$trunk"
check "check passes against the stamped headers" \
    bash -c '. "$1/utilities/pgf/coupling.sh"; PGF_ROOT="$1" pgf_coupling_check "$2" "$3" >/dev/null' _ "$PGF_ROOT" "$libdir" "$TEST_TMP/oldtrunk"

# build.sh --yade refuses before wmake.
"$PGF" exec --bare -- env YADE_TRUNK="$trunk" PGF_YADE_LIBDIR="$libdir" \
    "$BUILD_SH" --prefix "$TEST_TMP/prefix" --yade >"$TEST_TMP/build.log" 2>&1
rc=$?
check "build.sh --yade exits non-zero on a stale stamp (rc=$rc)" [ "$rc" -ne 0 ]
check "it prints the rebuild command" grep -q "rebuild coupling: .*buildCoupling.sh --libdir $libdir" "$TEST_TMP/build.log"
check "nothing was built" bash -c '! grep -q "Building " "$1"' _ "$TEST_TMP/build.log"

# pgf --yade refuses a prefix whose stamp does not match YADE_TRUNK.
prefix="$TEST_TMP/yprefix"
mkdir -p "$prefix/bin" "$prefix/lib"
printf '#!/bin/sh\n' >"$prefix/bin/porousGasificationFoam"
chmod +x "$prefix/bin/porousGasificationFoam"
echo '{"variant": "yade1"}' >"$prefix/.pgf-build.json"
cp "$libdir/$PGF_COUPLING_STAMP" "$prefix/lib/"
YADE_TRUNK="$trunk" YADE_BIN=/bin/true "$PGF" exec --prefix "$prefix" --yade -- true 2>"$TEST_TMP/env.err"
rc=$?
check "pgf --yade exits 2 on a stale stamp (rc=$rc)" [ "$rc" -eq 2 ]
check "pgf names the rebuild command" grep -q "rebuild coupling: .*buildCoupling.sh" "$TEST_TMP/env.err"
YADE_TRUNK="$TEST_TMP/oldtrunk" YADE_BIN=/bin/true "$PGF" exec --prefix "$prefix" --yade -- true 2>/dev/null
check "pgf --yade accepts a matching stamp" [ $? -eq 0 ]

# Build key: a copy of the sources in a scratch repo whose pin we can move.
repo="$TEST_TMP/repo"
mkdir -p "$repo"
cp -r "$PGF_ROOT"/{utilities,porousGasificationMedia,porousGasificationFoam} "$repo/" || die "cannot copy sources"
git -C "$repo" init -q || die "git init failed"
pin() { git -C "$repo" update-index --add --cacheinfo "160000,$1,submodules/Foam-Yade"; }
key() {
    env WM_PROJECT_VERSION=v2406 PGF_BUILD_ROOT="$TEST_TMP/br" YADE_TRUNK="$2" \
        "$repo/utilities/pgf/build.sh" --print-key "$1" 2>/dev/null | tail -1
}
pin 1111111111111111111111111111111111111111
k1="$(key --yade "$trunk")" n1="$(key --no-DEM "$trunk")"
pin 2222222222222222222222222222222222222222
k2="$(key --yade "$trunk")" n2="$(key --no-DEM "$trunk")"
k3="$(key --yade "$TEST_TMP/oldtrunk")" n3="$(key --no-DEM "$TEST_TMP/oldtrunk")"
check "yade1 keys are formed ($k1)" grep -qE "^[0-9a-f]{12}-yade1-" <<<"$k1"
check "moving the pin changes the yade1 key" [ "$k1" != "$k2" ]
check "other coupling headers change the yade1 key" [ "$k2" != "$k3" ]
check "the yade0 key ignores pin and trunk" bash -c '[ "$1" = "$2" ] && [ "$2" = "$3" ]' _ "$n1" "$n2" "$n3"
check "the yade0 key matches this checkout's" [ "$n1" = "$(tree_prefix | xargs basename)" ]

# Compile-time guard, against fake FoamYade.H headers.
guard() {
    local d="$TEST_TMP/guard$1"
    mkdir -p "$d"
    [ "$1" = none ] && echo '' >"$d/FoamYade.H" || echo "#define FOAMYADE_COUPLING_API $1" >"$d/FoamYade.H"
    echo '#include "foamYadeApiCheck.H"' >"$d/t.C"
    g++ -std=c++14 -fsyntax-only -I"$d" -I"$PGF_ROOT/porousGasificationMedia/DEM" "$d/t.C" 2>"$d/err"
}
check "guard passes on the required API" guard 1
check "guard passes when Foam-Yade has no macro yet" guard none
guard 2
check "guard fails on another API (rc=$?)" [ -s "$TEST_TMP/guard2/err" ]
check "guard fails on another API, naming it" grep -q "Foam-Yade coupling API differs" "$TEST_TMP/guard2/err"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- build.log"; tail -20 "$TEST_TMP/build.log"; echo "--- env.err"; cat "$TEST_TMP/env.err"; }
finish
