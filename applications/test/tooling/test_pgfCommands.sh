#!/bin/bash
# pgf setup / config / shell / run / build / tools and the root shims (#257 B,
# #264 PR 1, #274 A), with a fake bashrc, prefix and yade on a copy of pgf, so nothing
# is built and neither the real user config nor this checkout's .pgf.conf is touched.

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
export WM_PROJECT_DIR=/fake WM_PROJECT_VERSION=fake WM_OPTIONS=linux64GccDPInt32Opt
export WM_PROJECT_USER_DIR="$HOME/fake-user"
EOF

tree="$TEST_TMP/tree"
mkdir -p "$tree/utilities/pgf"
cp "$PGF" "$PGF_ROOT"/{build.sh,pgf-test,Allwmake,Allwclean} "$tree/" && cp "$PGF_ROOT"/utilities/pgf/* "$tree/utilities/pgf/" || die "cannot copy pgf"

prefix="$TEST_TMP/prefix"
mkdir -p "$prefix/bin" "$prefix/lib"
printf '#!/bin/sh\necho fake solver\n' >"$prefix/bin/porousGasificationFoam"
echo '{"variant": "yade1"}' >"$prefix/.pgf-build.json"
yadedir="$TEST_TMP/yade-install/bin"
mkdir -p "$yadedir"
printf '#!/bin/sh\necho fake yade\n' >"$yadedir/yade-2026.1"
chmod +x "$prefix/bin/porousGasificationFoam" "$yadedir/yade-2026.1"

trunk="$TEST_TMP/trunk"
mkdir -p "$trunk/pkg/openfoam/coupling/FoamYade"
echo '// fake' >"$trunk/pkg/openfoam/coupling/FoamYade/FoamYade.H"

# penv ARGS — pgf for a clean caller (HOME, TMPDIR, a PATH with only a suffixed yade).
penv() {
    env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" \
        "$tree/pgf" "$@"
}
# root TOOL ARGS — a root tool other than pgf, for the same caller.
root() {
    local t="$1"
    shift
    env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" \
        "$tree/$t" "$@"
}

# setup writes a file that status reads back.
penv setup --checkout --yes --no-link --foam-bashrc "$bashrc" --set PGF_RUN_ROOT="$TEST_TMP/runs" \
    >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?
check "setup --checkout --yes exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "setup --no-link made no link" [ ! -e "$home/.local/bin/pgf" ]
check "setup wrote the checkout file" grep -qx "PGF_FOAM_BASHRC=$bashrc" "$tree/.pgf.conf"
check "setup found the suffixed yade on the caller's PATH" \
    grep -qx "YADE_BIN=$yadedir/yade-2026.1" "$tree/.pgf.conf"
penv config >"$TEST_TMP/status" 2>&1
check "config reads PGF_FOAM_BASHRC back from the checkout file" \
    grep -qE "^  PGF_FOAM_BASHRC +$bashrc +\[checkout " "$TEST_TMP/status"
printf 'PGF_BUILD_ROOT=/keep\n' >>"$tree/.pgf.conf"
penv setup --checkout --yes --no-link --set PGF_MPI=loopback >/dev/null 2>&1
check "a second setup keeps existing keys and adds the new one" \
    bash -c 'grep -qx "PGF_BUILD_ROOT=/keep" "$1" && grep -qx "PGF_MPI=loopback" "$1" && grep -qx "PGF_FOAM_BASHRC=$2" "$1"' \
    _ "$tree/.pgf.conf" "$bashrc"
penv setup --user --yes --foam-bashrc "$bashrc" >/dev/null 2>&1
check "setup --user writes ~/.config/pgf/env.conf" grep -qx "PGF_FOAM_BASHRC=$bashrc" "$home/.config/pgf/env.conf"
check "  and links ~/.local/bin/pgf to this checkout" \
    [ "$(readlink -f "$home/.local/bin/pgf")" = "$(readlink -f "$tree/pgf")" ]
out="$(env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" \
    "$home/.local/bin/pgf" config get PGF_FOAM_BASHRC 2>&1)"
check "pgf called through the ~/.local/bin/pgf symlink finds its checkout ($out)" [ "$out" = "$bashrc" ]

# setup prints one step line per action, then what it saved where, then next: (#287).
check "setup steps: OpenFOAM is not v2406, with the fix under it" \
    bash -c 'grep -q "^  → looking for OpenFOAM … WARN fake, .*; PGF targets v2406$" "$1" &&
        grep -A1 "looking for OpenFOAM" "$1" | grep -q "^       fix: pgf config set PGF_FOAM_BASHRC="' _ "$TEST_TMP/out"
check "  the yade it found, and why its pin cannot be checked" \
    bash -c 'grep -q "^  → looking for yade … WARN $2/yade-2026.1$" "$1" &&
        grep -q "^       does not say which Foam-Yade commit built it" "$1"' _ "$TEST_TMP/out" "$yadedir"
check "  the file it wrote" grep -q "^  → saving settings … OK wrote .*\.pgf\.conf (checkout layer)$" "$TEST_TMP/out"
check "  that --no-link left pgf unlinked" grep -q "^  → linking pgf … skipped (--no-link)" "$TEST_TMP/out"
check "  then what was saved where, one key a line" \
    bash -c 'grep -q "^saved in .*\.pgf\.conf (checkout layer):$" "$1" && grep -qE "^  PGF_FOAM_BASHRC +$2 +\(new\)$" "$1"' \
    _ "$TEST_TMP/out" "$bashrc"
check "  and next: build, doctor and help config" \
    bash -c 'grep -qx "next:" "$1" && grep -qE "^  [^ ]*pgf build .*1.5 min" "$1" && grep -q "pgf doctor " "$1" && grep -q "pgf help config " "$1"' \
    _ "$TEST_TMP/out"
check "  without the explanations, which are in pgf help setup / config" \
    bash -c '! grep -q "clean environment" "$1" && ! grep -q "sourced before every build" "$1" && ! grep -q "later winning" "$1"' \
    _ "$TEST_TMP/out"
check "  and writes no colour codes into a file" bash -c '! grep -q $'"'"'\e'"'"' "$1"' _ "$TEST_TMP/out"
penv --color=always setup --checkout --yes --no-link --no-completion >"$TEST_TMP/outc" 2>&1
check "--color=always colours the step labels" grep -q $'\e\[33mWARN\e\[0m' "$TEST_TMP/outc"
NO_COLOR=1 penv --color never status >/dev/null 2>&1
check "--color never is accepted" [ $? -eq 0 ]
penv --color=sometimes status >/dev/null 2>"$TEST_TMP/err"
check "  and a bad --color value is refused" grep -q "color takes auto, always or never" "$TEST_TMP/err"

# help: one page per command, the same as <cmd> --help (#285)
penv help >"$TEST_TMP/help" 2>&1
check "pgf help has Usage, Description, the Settings/Build/Run/Other groups, Help Topics, Options, Examples, in order" \
    bash -c '[ "$(grep -E "^(Usage|Description|[A-Za-z]+ Commands|Help Topics|Options|Examples):$" "$1" | tr "\n" " ")" = \
        "Usage: Description: Settings Commands: Build Commands: Run Commands: Other Commands: Help Topics: Options: Examples: " ] && grep -q "^Use \"pgf help <command>\"" "$1"' _ "$TEST_TMP/help"
check "  and pgf --help is the same page" cmp -s "$TEST_TMP/help" <(penv --help 2>&1)
check "  the commands grouped as doc/pgf.md groups them" \
    [ "$(sed -n '/Commands:$/,/^$/s/^  \([a-z]*\) .*/\1/p' "$TEST_TMP/help" | tr '\n' ' ')" = \
        "setup config doctor build builds use gc copy run view shell test status pack tools help " ]
check "  then the old names, without completion and the old names as rows" \
    bash -c 'grep -q "Old names still work: where (status), ls (builds), exec (shell --)" "$1" &&
        ! grep -qE "^  (completion|where|ls|exec) " "$1"' _ "$TEST_TMP/help"
for c in $(sed -n '/Commands:/,/^$/s/^  \([a-z]*\) .*/\1/p' "$TEST_TMP/help"); do
    check "pgf help $c = pgf $c --help, with Usage and Description" \
        bash -c 'cmp -s <("$1" help "$2" 2>&1) <("$1" "$2" --help 2>&1) && "$1" help "$2" | grep -q "^Description:$"' \
        _ "$tree/pgf" "$c"
done
penv setup --yes --help >"$TEST_TMP/help" 2>&1
check "setup --help explains --yes, --checkout and --link-only" \
    bash -c 'grep -qE "^  -y, --yes +do not ask" "$1" && grep -qE "^  --checkout +save to" "$1" && grep -qE "^  --link-only +only link" "$1"' _ "$TEST_TMP/help"
check "  its Usage names --foam-bashrc, --yade-bin, --yade-trunk and --set" \
    bash -c 'sed -n "/^Usage:/,/^\$/p" "$1" | tr "\n" " " | grep -q -- "--foam-bashrc FILE.*--yade-bin FILE.*--yade-trunk DIR.*--set KEY=VALUE"' _ "$TEST_TMP/help"
check "  and says what setup is for" grep -q "pgf runs every build and case in a clean environment" "$TEST_TMP/help"
check "pgf help where shows status's page, saying where is its old name" \
    bash -c '"$1" help where | head -1 | grep -q "old name for pgf status" && "$1" where --help | grep -q "^pgf status:"' _ "$tree/pgf"
check "run --help lists the environment options" bash -c '"$1" run --help | grep -qE "^  --use SEL +the build"' _ "$tree/pgf"
check "config --help gives each key a meaning" bash -c '"$1" help config | grep -qE "^  PGF_MPI +Open MPI"' _ "$tree/pgf"
penv help nosuch >/dev/null 2>"$TEST_TMP/err"
check "help of an unknown command fails (rc=$?)" grep -q "unknown command or topic 'nosuch'" "$TEST_TMP/err"

# help topics and help --all replace doc/pgf-reference.md (#280)
penv help >"$TEST_TMP/help" 2>&1
topics="$(sed -n '/^Help Topics:$/,/^$/s/^  \([a-z-]*\) .*/\1/p' "$TEST_TMP/help")"
check "pgf help lists the topics: build-selection, run-record, environment" \
    [ "$(tr '\n' ' ' <<<"$topics")" = "build-selection run-record environment " ]
for t in $topics; do
    check "pgf help $t is its page" bash -c '"$1" help "$2" | head -1 | grep -q "^pgf help $2: "' _ "$tree/pgf" "$t"
done
keep="$(sed -n 's/^ *for v in \(HOME .*\); do$/\1/p' "$tree/pgf")"
check "the environment topic names every variable pgf keeps ($keep)" \
    bash -c 'page="$("$1" help environment | tr "\n" " ")"; for v in $2 "PGF_\*" "YADE_\*"; do
        grep -qw -- "$v" <<<"$page" || exit 1; done' _ "$tree/pgf" "$keep"
check "  NO_COLOR among them" grep -qw NO_COLOR <<<"$keep"
penv help --all >"$TEST_TMP/all" 2>&1
check "help --all piped exits 0 and prints plain text" \
    bash -c '[ "$2" -eq 0 ] && ! grep -q $'"'"'\e'"'"' "$1"' _ "$TEST_TMP/all" $?
check "  starting with the pgf page" cmp -s <(head -n "$(wc -l <"$TEST_TMP/help")" "$TEST_TMP/all") "$TEST_TMP/help"
for c in $(sed -n '/Commands:/,/^$/s/^  \([a-z]*\) .*/\1/p' "$TEST_TMP/help") completion; do
    check "  with the page of $c" grep -q "^pgf $c: " "$TEST_TMP/all"
done
for t in $topics; do check "  and of the topic $t" grep -q "^pgf help $t: " "$TEST_TMP/all"; done
printf '#!/bin/sh\necho "pager LESS=$LESS"; wc -l\n' >"$TEST_TMP/pager" && chmod +x "$TEST_TMP/pager"
out="$(env -i PATH=/usr/bin:/bin HOME="$home" TERM=dumb PAGER="$TEST_TMP/pager" \
    script -qec "$tree/pgf help --all" /dev/null 2>&1 | tr -d '\r')"
check "  on a terminal it goes through \$PAGER with LESS=FRX" \
    bash -c 'grep -qx "pager LESS=FRX" <<<"$1" && grep -qx "$2" <<<"$1"' _ "$out" "$(wc -l <"$TEST_TMP/all")"

echo "# hand-written comment" >>"$tree/.pgf.conf"
penv setup --checkout --yes --no-link --no-completion >"$TEST_TMP/out2" 2>&1
check "a setup that changes nothing says unchanged, not wrote" \
    bash -c 'grep -q "saving settings … unchanged .* (checkout layer)" "$1" && grep -q "^kept in .*, unchanged:$" "$1" && ! grep -q "wrote" "$1"' _ "$TEST_TMP/out2"
check "  shows kept values" grep -qE "^ +PGF_FOAM_BASHRC +$bashrc +\(kept\)$" "$TEST_TMP/out2"
check "  and leaves the file untouched" grep -qx "# hand-written comment" "$tree/.pgf.conf"
check "  and does not claim a completion install with --no-completion" \
    bash -c '! grep -q "completion" "$1"' _ "$TEST_TMP/out2"
bashrc2406="$FAKE/foam2406/etc/bashrc"
mkdir -p "${bashrc2406%/*}"
echo 'export WM_PROJECT_VERSION=v2406' >"$bashrc2406"
home2="$TEST_TMP/home2"  # a fresh user, no yade, and no checkout file in the way
mkdir -p "$home2"
mv "$tree/.pgf.conf" "$tree/.pgf.conf.keep"
env -i PATH=/usr/bin:/bin HOME="$home2" TMPDIR="$TEST_TMP" "$tree/pgf" setup --user --yes --no-completion \
    --foam-bashrc "$bashrc" >/dev/null 2>&1
env -i PATH=/usr/bin:/bin HOME="$home2" TMPDIR="$TEST_TMP" "$tree/pgf" setup --user --yes --no-completion \
    --foam-bashrc "$bashrc2406" >"$TEST_TMP/out3" 2>&1
check "setup reports OpenFOAM v2406 as OK" grep -q "→ looking for OpenFOAM … OK v2406, " "$TEST_TMP/out3"
check "  shows a changed value with the old one" grep -qE "^ +PGF_FOAM_BASHRC +.*foam2406/etc/bashrc +\(was $bashrc\)$" "$TEST_TMP/out3"
check "  says yade is optional when none is found" grep -q "→ looking for yade … none (only DEM runs need it" "$TEST_TMP/out3"
check "  gives the PATH fix when ~/.local/bin is not on PATH" \
    grep -qF 'export PATH="$HOME/.local/bin:$PATH"' "$TEST_TMP/out3"
env -i PATH="$home2/.local/bin:/usr/bin:/bin" HOME="$home2" TMPDIR="$TEST_TMP" "$tree/pgf" setup --user --yes \
    >"$TEST_TMP/out4" 2>&1
mv "$tree/.pgf.conf.keep" "$tree/.pgf.conf"
check "  and OK when it is" bash -c 'grep -q "→ linking pgf … OK ~/.local/bin/pgf → $2/pgf, on PATH" "$1" && ! grep -q "export PATH" "$1"' _ "$TEST_TMP/out4" "$tree"
check "  and says where completion went and that new shells pick it up" \
    grep -q "→ installing bash completion … OK ~/.local/share/bash-completion/completions/pgf; new shells pick it up" "$TEST_TMP/out4"
check "  without the step lines the summary repeats" \
    bash -c '! grep -qE "^pgf: (.*already points here|bash completion in|.* -> )" "$1"' _ "$TEST_TMP/out4"

# setup in a second checkout, while ~/.local/bin/pgf points at another one
home3="$TEST_TMP/home3"
mkdir -p "$home3/.local/bin" "$home3/other"
printf '#!/bin/sh\n' >"$home3/other/pgf" && chmod +x "$home3/other/pgf"
ln -s "$home3/other/pgf" "$home3/.local/bin/pgf"
mv "$tree/.pgf.conf" "$tree/.pgf.conf.keep"
env -i PATH="$home3/.local/bin:/usr/bin:/bin" HOME="$home3" TMPDIR="$TEST_TMP" "$tree/pgf" setup --user --yes \
    --foam-bashrc "$bashrc" >"$TEST_TMP/out5" 2>&1
rc=$?
check "setup with the link pointing at another checkout exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "  leaves the link alone" [ "$(readlink "$home3/.local/bin/pgf")" = "$home3/other/pgf" ]
check "  still installs completion and prints the outcome" \
    bash -c '[ -f "$2/.local/share/bash-completion/completions/pgf" ] && grep -qx "pgf setup" "$1" && grep -q "^saved in " "$1"' _ "$TEST_TMP/out5" "$home3"
check "  without the pre-summary --force lines" bash -c '! grep -q "rerun with --force" "$1"' _ "$TEST_TMP/out5"
check "  warns with a ~ path and the --link-only --force fix" \
    bash -c 'grep -q "→ linking pgf … WARN pgf on PATH is ~/other/pgf, not $2/pgf" "$1" && grep -q "fix: ./pgf setup --link-only --force" "$1"' _ "$TEST_TMP/out5" "$tree"
check "  and its next steps call ./pgf" \
    bash -c 'grep -q "^  ./pgf doctor " "$1" && grep -q "^  ./pgf help config " "$1"' _ "$TEST_TMP/out5"
env -i PATH=/usr/bin:/bin HOME="$home3" TMPDIR="$TEST_TMP" "$tree/pgf" setup --user --yes --no-completion \
    >"$TEST_TMP/out6" 2>&1
check "  off PATH, it says where the link points" \
    bash -c 'grep -q "→ linking pgf … WARN ~/.local/bin/pgf points at ~/other/pgf, not here" "$1" && grep -q "fix: ./pgf setup --link-only --force" "$1"' _ "$TEST_TMP/out6"
env -i PATH=/usr/bin:/bin HOME="$home3" TMPDIR="$TEST_TMP" "$tree/pgf" setup --link-only >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "  while --link-only still refuses with exit 2 and says why (rc=$rc)" \
    bash -c '[ "$1" -eq 2 ] && grep -q "rerun with --force" "$2"' _ "$rc" "$TEST_TMP/err"
mv "$tree/.pgf.conf.keep" "$tree/.pgf.conf"

# config
check "config get reads the resolved value" [ "$(penv config get PGF_MPI)" = loopback ]
penv config get PGF_YADE_LIBDIR >/dev/null 2>&1
check "config get of an unset key exits 1" [ $? -eq 1 ]
penv config get NOPE >/dev/null 2>&1
check "config get of an unknown key exits 2" [ $? -eq 2 ]
penv config set PGF_YADE_LIBDIR=/opt/libs --checkout 2>/dev/null
check "config set --checkout writes the checkout file" grep -qx "PGF_YADE_LIBDIR=/opt/libs" "$tree/.pgf.conf"
check "  and keeps the other keys" grep -qx "PGF_MPI=loopback" "$tree/.pgf.conf"
penv config set PGF_MPI=sometimes 2>/dev/null
check "config set rejects a bad value with exit 2" [ $? -eq 2 ]
penv config unset PGF_YADE_LIBDIR --checkout 2>/dev/null
check "config unset removes the key" bash -c '! grep -q PGF_YADE_LIBDIR "$1"' _ "$tree/.pgf.conf"
penv config --json >"$TEST_TMP/json"
check "config --json gives value and layer per key" python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
s = d["settings"]
assert s["PGF_MPI"]["value"] == "loopback" and s["PGF_MPI"]["layer"] == "checkout"
assert s["PGF_YADE_LIBDIR"]["value"] is None and d["files"]["checkout"]["exists"]
' "$TEST_TMP/json"
penv --prefix x >/dev/null 2>&1
check "options without a subcommand exit 2" [ $? -eq 2 ]

# shell
out="$(penv shell --prefix "$prefix" -c 'command -v porousGasificationFoam; echo "active=$PGF_ENV_ACTIVE"' 2>"$TEST_TMP/err")"
check "shell -c resolves the solver into the prefix" grep -qx "$prefix/bin/porousGasificationFoam" <<<"$out"
check "shell -c runs with PGF_ENV_ACTIVE=1" grep -qx "active=1" <<<"$out"
out="$(echo 'echo "PS1=$PS1"; exit' | penv shell --prefix "$prefix" 2>/dev/null)"
check "interactive shell sets the (pgf:<worktree>@<key>) prompt" grep -qF "PS1=(pgf:tree@prefix)" <<<"$out"

# run
case="$TEST_TMP/cases/plain"
mkdir -p "$case/constant"
printf '#!/bin/sh\npwd >ran.txt; command -v yade >>ran.txt; echo "args=$*" >>ran.txt\n' >"$case/Allclean"
cp "$case/Allclean" "$case/Allrun"
chmod +x "$case/Allclean" "$case/Allrun"
(cd / && penv run --prefix "$prefix" "$case" Allclean a b) >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "run <case> Allclean from / exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "the script ran inside the case with its arguments" \
    bash -c 'grep -qx "$1" "$1/ran.txt" && grep -qx "args=a b" "$1/ran.txt"' _ "$case"
check "a case without yadeProperties gets no yade" bash -c '! grep -q yade "$1/ran.txt"' _ "$case"
python3 -c '
import json, sys
r = json.load(open(sys.argv[1] + "/.pgf-run.json"))
assert r["exit_status"] == 0 and r["command"] == ["./Allclean", "a", "b"], r
assert r["build"]["prefix"] == sys.argv[2] and r["build"]["source"] == "--prefix", r
assert r["started_utc"] and r["finished_utc"] and r["yade"] is None, r
' "$case" "$prefix" 2>"$TEST_TMP/rec.err"
check "run writes <case>/.pgf-run.json (command, build, times, exit status)" [ $? -eq 0 ]
check "run's banner says in words what it uses: OpenFOAM, then the build and what chose it" \
    bash -c 'grep -qx "  INFO using OpenFOAM fake ($3)" "$1" && grep -qx "  INFO using build $2 (DEM, the prefix you gave)" "$1"' \
    _ "$TEST_TMP/err" "$prefix" "$bashrc"
check "run without time dirs prints OK and no pgf view hint" \
    bash -c 'grep -q "^  OK   Allclean in [0-9]*s" "$1" && ! grep -q "pgf view" "$1"' _ "$TEST_TMP/err"
printf '#!/bin/sh\nseq -f "header %%g" 30 >log.solver; printf "near\\nFOAM FATAL ERROR: bad\\n" >>log.solver\nseq -f "after %%g" 40 >>log.solver; exit 3\n' \
    >"$case/Allfail" && chmod +x "$case/Allfail"
(cd / && penv run --prefix "$prefix" "$case" Allfail) >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "run passes the script's exit status through (rc=$rc)" [ "$rc" -eq 3 ]
check "and records it" grep -q '"exit_status": 3' "$case/.pgf-run.json"
check "a failed run says FAIL with the exit status" grep -q "^  FAIL Allfail (exit 3) after " "$TEST_TMP/err"
check "  and shows the newest log from just before its FOAM FATAL message" \
    bash -c 'grep -q "log.solver from its FOAM FATAL message" "$1" && grep -qx "    header 30" "$1" &&
        grep -qx "    FOAM FATAL ERROR: bad" "$1" && ! grep -qx "    header 29" "$1"' _ "$TEST_TMP/err"
printf '#!/bin/sh\nseq -f "plain %%g" 30 >log.solver; exit 3\n' >"$case/Allfail"
(cd / && penv run --prefix "$prefix" "$case" Allfail) >/dev/null 2>"$TEST_TMP/err"
check "  or its last 20 lines without one" \
    bash -c 'grep -q "last 20 lines of log.solver" "$1" && grep -qx "    plain 30" "$1" && ! grep -qx "    plain 10" "$1"' _ "$TEST_TMP/err"
rm -f "$case/log.solver"
printf '#!/bin/sh\nexit 4\n' >"$case/Allfail"
(cd / && penv run --prefix "$prefix" "$case" Allfail) >/dev/null 2>"$TEST_TMP/err"
check "  or says no log was written" grep -q "no log.\* written by this run" "$TEST_TMP/err"
printf '#!/bin/sh\nmkdir -p 0 0.5 0.25; echo done >log.porousGasificationFoam\n' >"$case/Allok" && chmod +x "$case/Allok"
(cd / && penv run --prefix "$prefix" "$case" Allok) >/dev/null 2>"$TEST_TMP/err"
check "a run with results gives the latest time, last log and the pgf view next step" \
    bash -c 'grep -q "^  OK   Allok in " "$1" && grep -q "latest time: 0.5$" "$1" &&
        grep -q "last log: *log.porousGasificationFoam" "$1" && grep -qF "  pgf view $2 " "$1" && grep -qx "next:" "$1"' _ "$TEST_TMP/err" "$case"
rm -rf "$case/Allok" "$case/0" "$case/0.5" "$case/0.25" "$case/log.porousGasificationFoam"
# A copy with stale output: runApplication skips each step whose log.* exists.
mkdir -p "$case/2" "$case/processor0" && echo old >"$case/log.blockMesh"
printf '#!/bin/sh\n[ -f log.blockMesh ] || echo new >log.blockMesh\n' >"$case/Allskip" && chmod +x "$case/Allskip"
(cd / && penv run --prefix "$prefix" "$case" Allskip) >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "a run that skips every step exits as the script did (rc=$rc)" [ "$rc" -eq 0 ]
check "  warns up front that the case holds earlier output and how to clean it" \
    bash -c 'grep -q "already holds output from an earlier run: .*log.blockMesh" "$1" &&
        grep -q "processor0" "$1" && grep -qF "clean first: pgf run $2 Allclean" "$1"' _ "$TEST_TMP/err" "$case"
check "  says it ran nothing instead of OK, and gives no pgf view hint" \
    bash -c 'grep -q "^  WARN Allskip ran nothing" "$1" && ! grep -q "OK   Allskip" "$1" &&
        ! grep -q "latest time" "$1" && ! grep -q "pgf view" "$1"' _ "$TEST_TMP/err"
(cd / && penv run --prefix "$prefix" "$case" Allclean) >/dev/null 2>"$TEST_TMP/err"
check "  while Allclean on it gets neither warning" \
    bash -c '! grep -q "earlier run" "$1" && ! grep -q "ran nothing" "$1"' _ "$TEST_TMP/err"
printf '#!/bin/sh\necho new >log.blockMesh\n' >"$case/Allskip"
(cd / && penv run --prefix "$prefix" "$case" Allskip) >/dev/null 2>"$TEST_TMP/err"
check "  and a run that rewrites a stale log is OK" \
    bash -c 'grep -q "^  OK   Allskip in " "$1" && grep -q "last log: *log.blockMesh" "$1"' _ "$TEST_TMP/err"
rm -rf "$case/Allskip" "$case/2" "$case/processor0" "$case/log.blockMesh"
(cd / && penv run --prefix "$prefix" "$case") >/dev/null 2>"$TEST_TMP/err"
check "  and a clean case gets no earlier-output warning" \
    bash -c '! grep -q "earlier run" "$1"' _ "$TEST_TMP/err"
intree="$tree/tutorials/cases/plain"
mkdir -p "$intree" && cp "$case/Allrun" "$intree/"
(cd / && penv run --prefix "$prefix" "$intree") >/dev/null 2>"$TEST_TMP/err"
check "a case inside the checkout gets a warning to run on a copy made by pgf copy" \
    bash -c 'grep -q "is inside the checkout $2; .*git-ignored" "$1" && grep -q "Run on a copy: pgf copy plain$" "$1"' _ "$TEST_TMP/err" "$tree"
rm -rf "$tree/tutorials"
check "runs.jsonl in the XDG state dir gets the run" \
    grep -q "\"case\": \"$(realpath "$case")\"" "$home/.local/state/pgf/runs/runs.jsonl"
(cd / && env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" XDG_STATE_HOME="$TEST_TMP/xdg" \
    "$tree/pgf" run --prefix "$prefix" "$case" Allclean) >/dev/null 2>&1
check "  under XDG_STATE_HOME when it is set" grep -q '"case"' "$TEST_TMP/xdg/pgf/runs/runs.jsonl"
mkdir -p "$TEST_TMP/rostate" && chmod a-w "$TEST_TMP/rostate"
(cd / && env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" XDG_STATE_HOME="$TEST_TMP/rostate" \
    "$tree/pgf" run --prefix "$prefix" "$case" Allclean) >/dev/null 2>"$TEST_TMP/ro.err"
check "a read-only state dir (an image) still records the case, without a warning" \
    bash -c '[ -f "$1/.pgf-run.json" ] && ! grep -q "could not write" "$2"' _ "$case" "$TEST_TMP/ro.err"
chmod u+w "$TEST_TMP/rostate"
rm -f "$case/Allfail" "$case/.pgf-run.json"
rm -f "$case/ran.txt"
(cd "$TEST_TMP/cases" && penv run --prefix "$prefix" plain) >/dev/null 2>&1
check "a relative case dir resolves from the caller and Allrun is the default" [ -f "$case/ran.txt" ]

dem="$TEST_TMP/cases/dem"
cp -r "$case" "$dem" && rm -f "$dem/ran.txt"
printf 'FoamFile { object yadeProperties; }\nactive          true;\n' >"$dem/constant/yadeProperties"
(cd / && penv run --prefix "$prefix" "$dem") >/dev/null 2>"$TEST_TMP/err"
check "an active yadeProperties selects --yade (yade shim on PATH)" grep -q '/pgf-bin/yade$' "$dem/ran.txt"
check "run says it picked --yade" grep -q "using --yade" "$TEST_TMP/err"
rm -f "$dem/ran.txt"
(cd / && penv run --no-yade --prefix "$prefix" "$dem") >/dev/null 2>&1
check "--no-yade overrides the auto choice" bash -c '! grep -q yade "$1/ran.txt"' _ "$dem"
penv run --prefix "$prefix" "$TEST_TMP/nope" >/dev/null 2>&1
check "a missing case dir exits 2" [ $? -eq 2 ]

# the link
ln -sfn /elsewhere/pgf "$home/.local/bin/pgf"
penv setup --link-only >/dev/null 2>&1
check "setup --link-only refuses to replace another link without --force" [ $? -eq 2 ]
penv setup --link-only --force >/dev/null 2>"$TEST_TMP/err"
check "setup --link-only still prints its link line" grep -q "^pgf: .*/.local/bin/pgf -> " "$TEST_TMP/err"
check "setup --link-only --force replaces it" \
    [ "$(readlink -f "$home/.local/bin/pgf")" = "$(readlink -f "$tree/pgf")" ]

# build hands --isolated (and --yade) to build.sh under exec --bare
printf '#!/bin/sh\necho "build.sh $*"; echo "user=$WM_PROJECT_USER_DIR"\n' >"$tree/utilities/pgf/build.sh"
chmod +x "$tree/utilities/pgf/build.sh"
out="$(penv build --yade --libs-only 2>"$TEST_TMP/err")"
check "pgf build's banner leaves the build line to build.sh, and MPI out" \
    bash -c 'grep -q "^  INFO using OpenFOAM" "$1" && ! grep -q "using no build" "$1" && ! grep -q MPI "$1"' _ "$TEST_TMP/err"
check "  and ends with the next: block" bash -c 'grep -qx "next:" "$1" && grep -q "pgf copy --list" "$1" && grep -q "pgf test fast" "$1"' _ "$TEST_TMP/err"
check "build runs build.sh --isolated --yade with the extra options" grep -qx "build.sh --isolated --yade --libs-only" <<<"$out"
check "  in a bare env (user dir is the build root)" grep -qx "user=/keep" <<<"$out"
out="$(root Allwmake --yade 2>/dev/null)"
check "Allwmake is pgf build --all --no-DEM" grep -qx "build.sh --isolated --all --no-DEM --yade" <<<"$out"
out="$(root Allwclean --purge 2>/dev/null)"
check "Allwclean is pgf build clean --all" grep -qx "build.sh --isolated clean --all --purge" <<<"$out"

# the root shims (#274)
out="$(root build.sh --print-key 2>"$TEST_TMP/err")"
check "the build.sh shim forwards to utilities/pgf/build.sh" grep -qx "build.sh --print-key" <<<"$out"
check "  and names the new command on stderr" grep -q "use: pgf build --print-key" "$TEST_TMP/err"
printf '#!/bin/sh\necho "test.sh $*"\n' >"$tree/utilities/pgf/test.sh"
chmod +x "$tree/utilities/pgf/test.sh"
out="$(penv test --status fast 2>/dev/null)"
check "pgf test runs utilities/pgf/test.sh" grep -qx "test.sh --status fast" <<<"$out"
out="$(root pgf-test --gc --dry-run 2>"$TEST_TMP/err")"
check "the pgf-test alias forwards to pgf test" grep -qx "test.sh --gc --dry-run" <<<"$out"
check "  and says it is deprecated" grep -q "pgf-test: deprecated; use: pgf test" "$TEST_TMP/err"
check "pgf-env is gone" [ ! -e "$PGF_ROOT/pgf-env" ]

# pgf status (where is its old name): files, builds, records, scratch, cases, the active build
for c in status where; do
    penv $c >"$TEST_TMP/where" 2>&1
    check "$c names the checkout and both settings files" \
        bash -c 'grep -qE "^checkout +$3 \(not a git checkout\)" "$1" && grep -qF "~/.config/pgf/env.conf" "$1" && grep -qF "$3/.pgf.conf" "$1"' \
        _ "$TEST_TMP/where" "$home" "$tree"
    check "  the build store and its layer" grep -qE "^builds +/keep \[checkout $tree/.pgf.conf:[0-9]+\]" "$TEST_TMP/where"
    check "  the run records, with ~" grep -qxF "records   ~/.local/state/pgf/runs" "$TEST_TMP/where"
    check "  scratch, as doc/pgf.md calls it" grep -qE "^scratch +$TEST_TMP/runs \[checkout " "$TEST_TMP/where"
    check "  the case root (\$WM_PROJECT_USER_DIR/run by default)" grep -qE "^cases +~/fake-user/run \[default: \\\$WM_PROJECT_USER_DIR/run\]" "$TEST_TMP/where"
    check "  the builds of the checkout, by name, and the case copies" \
        bash -c 'grep -qx "builds of tree:" "$1" && grep -qx "case copies (pgf copy), per branch:" "$1"' _ "$TEST_TMP/where"
    check "  and not the settings block (that is pgf config)" bash -c '! grep -q "^settings:" "$1"' _ "$TEST_TMP/where"
done
env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" PGF_BUILD=nosuchbuild \
    "$tree/pgf" status >"$TEST_TMP/where" 2>&1
check "status shows a terminal-selected build and that it was chosen there" \
    grep -qE "^build +nosuchbuild \[yade[01]; terminal: pgf use\] no build matches" "$TEST_TMP/where"

# pgf shell -- COMMAND runs it as exec did; exec is its old name
check "shell -- COMMAND runs the command with its arguments" \
    [ "$(penv shell --prefix "$prefix" -- printf '%s|' a 'b c' 2>/dev/null)" = "a|b c|" ]
check "exec -- COMMAND still works" [ "$(penv exec --prefix "$prefix" -- printf x 2>/dev/null)" = x ]
penv exec --prefix "$prefix" >/dev/null 2>"$TEST_TMP/err"
check "exec without a command still refuses (rc=$?)" grep -q "no command given" "$TEST_TMP/err"
penv shell --prefix "$prefix" -c true -- true >/dev/null 2>"$TEST_TMP/err"
check "shell refuses -c together with -- COMMAND" grep -q "not both" "$TEST_TMP/err"

# pgf tools: one what / why / instead block per root tool
penv tools >"$TEST_TMP/tools" 2>&1
for t in pgf Allwmake Allwclean build.sh pgf-test; do
    check "pgf tools explains $t" bash -c 'awk -v t="$2" "\$0 == t { f = 1; next } f && /^$/ { exit } f" "$1" |
        grep -c -E "^  (what|why|instead): " | grep -qx 3' _ "$TEST_TMP/tools" "$t"
done

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
