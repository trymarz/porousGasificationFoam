#!/bin/bash
# pgf copy (#287): a tutorial goes to $PGF_CASE_ROOT/<branch>/<case> with cp -rL
# and a record of its checkout and commit; pgf run of the copy uses that
# checkout's build and warns once the branch has moved on; pgf status lists
# copies per branch. Two fake checkouts (git repos with a stub build.sh), a
# fake bashrc and prefix: nothing is built.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

home="$TEST_TMP/home"
mkdir -p "$home"
bashrc="$FAKE/foam/etc/bashrc"
mkdir -p "${bashrc%/*}"
cat >"$bashrc" <<'EOF'
export WM_PROJECT_DIR=/fake WM_PROJECT_VERSION=v2606 WM_OPTIONS=linux64GccDPInt32Opt
export WM_PROJECT_USER_DIR="$HOME/u" FOAM_RUN="$HOME/u/elsewhere"
EOF

g() { git -C "$1" -c user.name=t -c user.email=t@example.invalid "${@:2}"; }

# mktree DIR PREFIX — a checkout: pgf, a build.sh stub resolving to PREFIX, one tutorial.
mktree() {
    local t="$1"
    mkdir -p "$t/utilities/pgf" "$t/doc" "$t/tutorials/cases/family/demo/system" "$t/tutorials/cases/family/common"
    cp "$PGF" "$t/" && cp -r "$PGF_ROOT"/utilities/pgf/* "$t/utilities/pgf/" || die "cannot copy pgf"
    printf '#!/bin/sh\ncase "$*" in *--print-prefix*) echo "%s" ;; *--print-key*) echo "${2##*/}" ;; esac\n' "$2" \
        >"$t/utilities/pgf/build.sh"
    printf '## Tutorial Cases\n\n| Case | What it is | Notes |\n|---|---|---|\n| `family/*` | A demo family | |\n' \
        >"$t/doc/case-setup.md"
    printf '#!/bin/sh\necho "$PGF_ROOT" >ran.txt\n' >"$t/tutorials/cases/family/demo/Allrun"
    chmod +x "$t/utilities/pgf/build.sh" "$t/tutorials/cases/family/demo/Allrun"
    echo 'FoamFile {}' >"$t/tutorials/cases/family/demo/system/controlDict"
    echo shared >"$t/tutorials/cases/family/common/data"
    ln -s ../common/data "$t/tutorials/cases/family/demo/data"
    git init -q -b main "$t" && g "$t" add -A && g "$t" commit -qm init || die "cannot make $t a git repo"
}
# mkprefix DIR — a complete fake build.
mkprefix() {
    mkdir -p "$1/bin" "$1/lib"
    printf '#!/bin/sh\n' >"$1/bin/porousGasificationFoam" && chmod +x "$1/bin/porousGasificationFoam"
    echo '{"variant": "yade0"}' >"$1/.pgf-build.json"
}
one="$TEST_TMP/one" two="$TEST_TMP/two"
mkprefix "$TEST_TMP/builds/aaaaaaaaaaaa-yade0-v2606-gcc0"
mkprefix "$TEST_TMP/builds/bbbbbbbbbbbb-yade0-v2606-gcc0"
mktree "$one" "$TEST_TMP/builds/aaaaaaaaaaaa-yade0-v2606-gcc0"
mktree "$two" "$TEST_TMP/builds/bbbbbbbbbbbb-yade0-v2606-gcc0"
g "$one" checkout -qb feature/x

# p TREE ARGS — that checkout's pgf for a clean caller.
p() {
    local t="$1"
    shift
    env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC="$bashrc" \
        PGF_BUILD_ROOT="$TEST_TMP/builds" "${extra[@]}" "$t/pgf" "$@"
}
extra=()

p "$one" copy --list >"$TEST_TMP/out" 2>&1
check "copy --list names each tutorial with its line from doc/case-setup.md" \
    grep -qE "^  family/demo +A demo family$" "$TEST_TMP/out"

p "$one" copy family/demo >"$TEST_TMP/out" 2>&1
rc=$?
dest="$home/u/run/feature-x/family/demo"
check "copy exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "  into \$WM_PROJECT_USER_DIR/run/<branch, not \$FOAM_RUN, / as ->/<tutorial>" [ -x "$dest/Allrun" ]
check "  following links (cp -rL)" bash -c '[ -f "$1/data" ] && [ ! -L "$1/data" ] && grep -qx shared "$1/data"' _ "$dest"
check "  and says where, with ~, and next: pgf run" \
    bash -c 'grep -q "^       to ~/u/run/feature-x/family/demo$" "$1" && grep -q "^  pgf run ~/u/run/feature-x/family/demo " "$1"' _ "$TEST_TMP/out"
check "  recording the checkout, branch and commit" python3 -c '
import json, sys
r = json.load(open(sys.argv[1] + "/.pgf-source.json"))
assert r["checkout"] == sys.argv[2] and r["branch"] == "feature/x" and r["commit"] == sys.argv[3], r
assert r["tutorial"] == "family/demo", r
' "$dest" "$(cd "$one" && pwd -P)" "$(g "$one" rev-parse HEAD)"

echo mine >"$dest/mine"
p "$one" copy family/demo >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "a second copy to the same place is refused (rc=$rc)" \
    bash -c '[ "$1" -eq 2 ] && grep -q "already exists; kept it" "$2" && [ -f "$3/mine" ]' _ "$rc" "$TEST_TMP/err" "$dest"
check "  and says how to replace it" grep -q "pgf copy family/demo --force" "$TEST_TMP/err"
p "$one" copy family/demo --force >/dev/null 2>&1
check "--force replaces it" bash -c '[ ! -e "$1/mine" ] && [ -x "$1/Allrun" ]' _ "$dest"
p "$one" copy tutorials/cases/family/demo demo2 >/dev/null 2>&1
check "NAME names the copy; a tutorials/cases/ path works too" [ -x "$home/u/run/feature-x/demo2/Allrun" ]
p "$one" copy nosuch >/dev/null 2>"$TEST_TMP/err"
check "an unknown tutorial is refused, pointing at --list" grep -q "see pgf copy --list" "$TEST_TMP/err"
extra=(PGF_CASE_ROOT="$TEST_TMP/cases")
p "$one" copy family/demo >/dev/null 2>&1
check "PGF_CASE_ROOT moves the copies" [ -x "$TEST_TMP/cases/feature-x/family/demo/Allrun" ]
extra=()
g "$two" checkout -q --detach
p "$two" copy family/demo >/dev/null 2>&1
check "a detached HEAD names the dir by its short commit" [ -x "$home/u/run/$(g "$two" rev-parse --short HEAD)/family/demo/Allrun" ]
g "$two" checkout -q main

# pgf run: the copy's checkout answers, even through another checkout's pgf
(cd / && p "$two" run "$dest") >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "run of a copy through another checkout's pgf exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "  and runs with the checkout the copy came from" grep -qx "$(cd "$one" && pwd -P)" "$dest/ran.txt"
check "  says where the copy came from" \
    grep -q "INFO copy of family/demo from feature/x @ $(g "$one" rev-parse --short HEAD) " "$TEST_TMP/err"
check "  and that the build is that checkout's" \
    grep -q "INFO using build aaaaaaaaaaaa (non-DEM, from .*one, where the case was copied from" "$TEST_TMP/err"
check "  with no stale warning while the branch has not moved" bash -c '! grep -q "moved on" "$1"' _ "$TEST_TMP/err"
(cd / && p "$two" run --prefix "$TEST_TMP/builds/bbbbbbbbbbbb-yade0-v2606-gcc0" "$dest") >/dev/null 2>&1
check "a chosen build (--prefix) keeps the run in the calling checkout" grep -qx "$(cd "$two" && pwd -P)" "$dest/ran.txt"

echo change >"$one/README" && g "$one" add README && g "$one" commit -qm change
(cd / && p "$one" run "$dest") >/dev/null 2>"$TEST_TMP/err"
check "after a new commit on the branch, run warns the copy is behind, with the fix" \
    grep -q "WARN feature/x has moved on since the copy (.*); .*pgf copy family/demo --force" "$TEST_TMP/err"

# Inside a pgf image (#297): no git, so no false "detached HEAD"; no hand-over.
nogit="$TEST_TMP/nogit" && mkdir -p "$nogit" && ln -s "$(command -v python3)" "$nogit/python3"
PATH="$nogit" python3 "$one/utilities/pgf/copies.py" check "$dest" >"$TEST_TMP/nogit.out"
check "without git, a copy is not reported stale" bash -c '[ -z "$(cut -f3 "$1")" ]' _ "$TEST_TMP/nogit.out"
check "  and says it was not checked" grep -q "not checked against its checkout (no git here)" "$TEST_TMP/nogit.out"
printf '{"pgf_commit": "%s", "variant": "yade0"}\n' "$(printf 'c%.0s' {1..40})" >"$TEST_TMP/manifest.json"
extra=(PGF_IMAGE_MANIFEST="$TEST_TMP/manifest.json")
(cd / && p "$two" run "$dest") >/dev/null 2>"$TEST_TMP/err"
extra=()
check "in an image (PGF_IMAGE_MANIFEST), run keeps the copy in the calling pgf" grep -qx "$(cd "$two" && pwd -P)" "$dest/ran.txt"
check "  and says it runs in the image" grep -q "INFO inside the pgf image cccccccccccc (yade0)" "$TEST_TMP/err"

p "$one" status >"$TEST_TMP/status" 2>&1
check "status lists the copies per branch" \
    bash -c 'sed -n "/^case copies/,/^\$/p" "$1" | grep -qx "  feature-x" &&
        sed -n "/^case copies/,/^\$/p" "$1" | grep -qE "^    family/demo +feature/x @ [0-9a-f]{7}  stale: feature/x has moved on"' _ "$TEST_TMP/status"
check "  and the detached copy under its commit" grep -qx "  $(g "$two" rev-parse --short HEAD)" "$TEST_TMP/status"
check "  and where they go (cases, \$WM_PROJECT_USER_DIR/run by default)" grep -qE '^cases +~/u/run \[default: \$WM_PROJECT_USER_DIR/run\]' "$TEST_TMP/status"

# A case whose Allrun calls a bare ../ script can't run from a copy (#290).
odd="$TEST_TMP/outside/needsParent"
mkdir -p "$odd/system" && printf '#!/bin/sh\ncd "${0%%/*}" || exit\n# ../comment is fine\n../helper 1\n' >"$odd/Allrun"
chmod +x "$odd/Allrun"
p "$one" copy "$odd" >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?
check "copy refuses a case whose Allrun calls a bare ../ script (rc=$rc)" [ "$rc" -eq 2 ]
check "  naming the line and the PGF_ROOT fix" \
    bash -c 'grep -q "can.t run from a copy" "$1" && grep -q "Allrun:4:../helper 1" "$1" && grep -q PGF_ROOT "$1"' _ "$TEST_TMP/err"
check "  and copies nothing" [ ! -e "$home/u/run/feature-x/needsParent" ]

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
