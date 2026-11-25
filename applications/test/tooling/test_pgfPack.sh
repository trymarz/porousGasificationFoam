#!/bin/bash
# pgf pack --dry-run (#264 PR 6): the build context and definition for a
# commit and its yade-pgf pin, refusals, and the manifest pin inside an image.
# No image is built.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

g() { git -C "$1" -c user.name=t -c user.email=t@t "${@:2}"; }

# A Yade repo with the PgfCoupling sources, and a scratch PGF commit pinning it.
fy="$TEST_TMP/fy"
mkdir -p "$fy/pkg/openfoam" && g "$fy" init -q
echo "// api" >"$fy/pkg/openfoam/PgfCoupling.hpp"
g "$fy" add -A && g "$fy" commit -qm pin
pin="$(g "$fy" rev-parse HEAD)"

tree="$TEST_TMP/pgf"
mkdir -p "$tree/submodules/yade-pgf" && cp -r "$PGF_ROOT/pgf" "$PGF_ROOT/utilities" "$PGF_ROOT/container" "$tree/"
g "$tree" init -q && g "$tree" add pgf utilities container &&
    g "$tree" update-index --add --cacheinfo "160000,$pin,submodules/yade-pgf" &&
    g "$tree" commit -qm scratch || die "cannot make the scratch commit"
head="$(g "$tree" rev-parse HEAD)"

pack() { PGF_PACK_DIR="$TEST_TMP/ctx" YADE_TRUNK="$fy" "$tree/pgf" pack "$@"; }

pack --dry-run >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?
cp "$TEST_TMP/ctx/pgf-pack.def" "$TEST_TMP/def" 2>/dev/null
check "pack --dry-run exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "the context holds the PGF commit" bash -c 'tar -tf "$1/pgf-src.tar" | grep -qx pgf' _ "$TEST_TMP/ctx"
check "and the pinned yade-pgf" bash -c 'tar -tf "$1/yade-pgf.tar" | grep -q PgfCoupling.hpp' _ "$TEST_TMP/ctx"
check "the definition uses the toolchain base image" \
    grep -qF "$(grep -m1 '^From:' "$PGF_ROOT/container/pgf-toolchain.def")" "$TEST_TMP/def"
check "the image installs the toolchain's pinned openfoam2606-dev" \
    grep -qF "\"openfoam2606-dev=$(sed -n 's/^[[:space:]]*FOAM_DEV_VERSION=//p' "$PGF_ROOT/container/pgf-toolchain.def")\"" "$TEST_TMP/def"
check "yade is built with the pin in its version" grep -q "YADE_VERSION=.*-git-${pin:0:7}" "$TEST_TMP/def"
check "the manifest records the commit and pin" \
    bash -c 'grep -q "\"pgf_commit\": \"$2\"" "$1" && grep -q "\"foam_yade_pin\": \"$3\"" "$1"' _ "$TEST_TMP/def" "$head" "$pin"
check "PGF builds the yade1 variant" grep -q "pgf build --yade" "$TEST_TMP/def"
check "the pin manifest exists before pgf build" \
    bash -c 'awk "/manifest.json\", \"w\"/{m=NR} /pgf build --yade/{b=NR} END{exit !(m && b && m<b)}" "$1"' _ "$TEST_TMP/def"
check "the image defaults to the yade1 variant" grep -q "PGF_YADE=1" "$TEST_TMP/def"
check "the default output names the commit" grep -q "pgf-${head:0:12}-yade1.sif" "$TEST_TMP/err"
check "the dry run prints no definition, only where it is" \
    bash -c '[ ! -s "$1" ] && grep -q "OK   wrote the image definition" "$2"' _ "$TEST_TMP/out" "$TEST_TMP/err"
check "  says the context stays, and next: the real build with its time" \
    bash -c 'grep -q "stays until you remove it" "$1" && grep -q "^next:" "$1" && grep -q "30-60 min" "$1"' _ "$TEST_TMP/err"
check "yade1 names the yade-pgf pin" grep -q "compiles yade-pgf ${pin:0:10}" "$TEST_TMP/err"
# The runscript passes the current pgf commands through, not the pre-#287 names.
rs="$(sed -n '/^%runscript/,/^%/p' "$TEST_TMP/def" | grep -m1 'status |')"
for c in status doctor config builds copy shell run view test tools help; do
    grep -qE "(^|[ |])$c( |\)|$)" <<<"$rs" || fail "the runscript passes $c through"
done
check "the runscript has no old names (exec, ls)" bash -c '! grep -qE "(^|[ |])(exec|ls)( |\)|$)" <<<"$1"' _ "$rs"
check "an image run doesn't warn about missing OpenFOAM tutorials" grep -q 'mkdir -p .*/tutorials' "$TEST_TMP/def"

mkdir -p "$TEST_TMP/here"
(cd "$TEST_TMP/here" && pack --dry-run --output rel.sif >/dev/null 2>"$TEST_TMP/err")
check "a relative --output is resolved against the caller's directory" grep -qF "$TEST_TMP/here/rel.sif" "$TEST_TMP/err"

rm -rf "$TEST_TMP/ctx"
pack --no-yade --dry-run >/dev/null 2>"$TEST_TMP/err0"
cp "$TEST_TMP/ctx/pgf-pack.def" "$TEST_TMP/def0"
check "--no-yade names no yade-pgf pin" bash -c '! grep -q yade-pgf "$1"' _ "$TEST_TMP/err0"
check "--no-yade: no yade in the image" bash -c '! grep -q "yade-pgf.tar\|cmake\|YADE_BIN" "$1"' _ "$TEST_TMP/def0"

echo "// edit" >>"$tree/pgf"
pack --dry-run >/dev/null 2>"$TEST_TMP/err"
check "a dirty checkout is refused (rc=$?)" grep -q "uncommitted changes" "$TEST_TMP/err"
g "$tree" checkout -q -- pgf

PGF_PACK_DIR="$TEST_TMP/ctx2" YADE_TRUNK="$TEST_TMP/nowhere" "$tree/pgf" pack --dry-run >/dev/null 2>"$TEST_TMP/err"
check "a pin missing from every trunk is refused" grep -q "is in neither YADE_TRUNK nor the submodule" "$TEST_TMP/err"

# Inside an image there is no git index: the pin comes from the manifest.
img="$TEST_TMP/img"
mkdir -p "$img" && cp -r "$PGF_ROOT/utilities" "$img/"
printf '{"foam_yade_pin": "%s"}\n' "$pin" >"$TEST_TMP/manifest.json"
got="$(PGF_ROOT="$img" PGF_IMAGE_MANIFEST="$TEST_TMP/manifest.json" bash -c '. "$PGF_ROOT/utilities/pgf/coupling.sh"; pgf_coupling_pin')"
check "without git, the pin is read from PGF_IMAGE_MANIFEST" [ "$got" = "$pin" ]

[ "$TEST_FAILURES" -eq 0 ] || cat "$TEST_TMP/err"
finish
