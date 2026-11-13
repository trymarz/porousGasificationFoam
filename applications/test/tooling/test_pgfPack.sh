#!/bin/bash
# pgf pack --dry-run (#264 PR 6): the build context and definition for a
# commit and its Foam-Yade pin, refusals, and the manifest pin inside an image.
# No image is built.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

g() { git -C "$1" -c user.name=t -c user.email=t@t "${@:2}"; }

# A Foam-Yade repo with coupling headers, and a scratch PGF commit pinning it.
fy="$TEST_TMP/fy"
mkdir -p "$fy/pkg/openfoam/coupling/FoamYade" && g "$fy" init -q
echo "// api" >"$fy/pkg/openfoam/coupling/FoamYade/FoamYade.H"
g "$fy" add -A && g "$fy" commit -qm pin
pin="$(g "$fy" rev-parse HEAD)"

tree="$TEST_TMP/pgf"
mkdir -p "$tree/submodules/Foam-Yade" && cp -r "$PGF_ROOT/pgf" "$PGF_ROOT/utilities" "$PGF_ROOT/container" "$tree/"
g "$tree" init -q && g "$tree" add pgf utilities container &&
    g "$tree" update-index --add --cacheinfo "160000,$pin,submodules/Foam-Yade" &&
    g "$tree" commit -qm scratch || die "cannot make the scratch commit"
head="$(g "$tree" rev-parse HEAD)"

pack() { PGF_PACK_DIR="$TEST_TMP/ctx" YADE_TRUNK="$fy" "$tree/pgf" pack "$@"; }

pack --dry-run >"$TEST_TMP/def" 2>"$TEST_TMP/err"
rc=$?
check "pack --dry-run exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "the context holds the PGF commit" bash -c 'tar -tf "$1/pgf-src.tar" | grep -qx pgf' _ "$TEST_TMP/ctx"
check "and the pinned Foam-Yade" bash -c 'tar -tf "$1/foam-yade.tar" | grep -q FoamYade.H' _ "$TEST_TMP/ctx"
check "the definition uses the toolchain base image" \
    grep -qF "$(grep -m1 '^From:' "$PGF_ROOT/container/pgf-toolchain.def")" "$TEST_TMP/def"
check "yade is built with the pin in its version" grep -q "YADE_VERSION=.*-git-${pin:0:7}" "$TEST_TMP/def"
check "the manifest records the commit and pin" \
    bash -c 'grep -q "\"pgf_commit\": \"$2\"" "$1" && grep -q "\"foam_yade_pin\": \"$3\"" "$1"' _ "$TEST_TMP/def" "$head" "$pin"
check "PGF builds the yade1 variant" grep -q "pgf build --yade" "$TEST_TMP/def"
check "the pin manifest exists before pgf build" \
    bash -c 'awk "/manifest.json\", \"w\"/{m=NR} /pgf build --yade/{b=NR} END{exit !(m && b && m<b)}" "$1"' _ "$TEST_TMP/def"
check "the image defaults to the yade1 variant" grep -q "PGF_YADE=1" "$TEST_TMP/def"
check "the default output names the commit" grep -q "pgf-${head:0:12}-yade1.sif" "$TEST_TMP/err"

rm -rf "$TEST_TMP/ctx"
pack --no-yade --dry-run >"$TEST_TMP/def0" 2>/dev/null
check "--no-yade: no yade in the image" bash -c '! grep -q "foam-yade.tar\|cmake\|YADE_BIN" "$1"' _ "$TEST_TMP/def0"

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
