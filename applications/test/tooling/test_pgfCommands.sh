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
penv status >"$TEST_TMP/status" 2>&1
check "status reads PGF_FOAM_BASHRC back from the checkout file" \
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

# setup ends with a summary of what it decided (#280).
check "setup summary says it wrote the file" grep -q "OK   settings: wrote .*\.pgf\.conf (checkout layer)" "$TEST_TMP/out"
check "  shows the new value" grep -qE "^ +PGF_FOAM_BASHRC +$bashrc \(new\)$" "$TEST_TMP/out"
check "  warns that the OpenFOAM version is not v2406, with a fix" \
    bash -c 'grep -q "WARN OpenFOAM fake: .*PGF targets v2406" "$1" && grep -q "fix: pgf config set PGF_FOAM_BASHRC=" "$1"' _ "$TEST_TMP/out"
check "  names the yade it found" grep -q "OK   yade: $yadedir/yade-2026.1" "$TEST_TMP/out"
check "  says --no-link left pgf unlinked" grep -q "pgf not linked (--no-link)" "$TEST_TMP/out"
check "  points at pgf config, pgf status and ./Allwmake" \
    bash -c 'grep -q "^  pgf config " "$1" && grep -q "^  pgf status " "$1" && grep -q "^  ./Allwmake .*1.5 min" "$1"' _ "$TEST_TMP/out"
echo "# hand-written comment" >>"$tree/.pgf.conf"
penv setup --checkout --yes --no-link --no-completion >"$TEST_TMP/out2" 2>&1
check "a setup that changes nothing says unchanged, not wrote" \
    bash -c 'grep -q "settings: .* unchanged (checkout layer)" "$1" && ! grep -q "wrote" "$1"' _ "$TEST_TMP/out2"
check "  shows kept values" grep -qE "^ +PGF_FOAM_BASHRC +$bashrc \(kept\)$" "$TEST_TMP/out2"
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
check "setup summary reports OpenFOAM v2406 as OK" grep -q "OK   OpenFOAM v2406: " "$TEST_TMP/out3"
check "  shows a changed value with the old one" grep -qE "^ +PGF_FOAM_BASHRC +.*foam2406/etc/bashrc \(was $bashrc\)$" "$TEST_TMP/out3"
check "  says yade is optional when none is found" grep -q "OK   yade: not found; DEM coupling (--yade) unavailable" "$TEST_TMP/out3"
check "  gives the PATH fix when ~/.local/bin is not on PATH" \
    grep -qF 'export PATH="$HOME/.local/bin:$PATH"' "$TEST_TMP/out3"
env -i PATH="$home2/.local/bin:/usr/bin:/bin" HOME="$home2" TMPDIR="$TEST_TMP" "$tree/pgf" setup --user --yes \
    >"$TEST_TMP/out4" 2>&1
mv "$tree/.pgf.conf.keep" "$tree/.pgf.conf"
check "  and OK when it is" bash -c 'grep -q "OK   pgf on PATH is this checkout" "$1" && ! grep -q "export PATH" "$1"' _ "$TEST_TMP/out4"
check "  and says where completion went and that it takes effect in a new shell" \
    grep -q "OK   bash completion: ~/.local/share/bash-completion/completions/pgf; takes effect in a new shell" "$TEST_TMP/out4"
check "  without the step lines the summary repeats" \
    bash -c '! grep -qE "^pgf: (.*already points here|bash completion in|.* -> )" "$1"' _ "$TEST_TMP/out4"

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
check "run's banner: OpenFOAM on one line, the prefix and how it was chosen on the next" \
    bash -c 'grep -qx "pgf: OpenFOAM fake.*" "$1" && grep -qx "pgf: prefix $2 (yade1, chosen by --prefix)" "$1"' \
    _ "$TEST_TMP/err" "$prefix"
check "run without time dirs prints OK and no pgfView hint" \
    bash -c 'grep -q "^pgf: Allclean OK in [0-9]*s" "$1" && ! grep -q pgfView "$1"' _ "$TEST_TMP/err"
printf '#!/bin/sh\nprintf "line1\\nFOAM FATAL ERROR: bad\\n" >log.solver\nexit 3\n' >"$case/Allfail" && chmod +x "$case/Allfail"
(cd / && penv run --prefix "$prefix" "$case" Allfail) >/dev/null 2>"$TEST_TMP/err"
rc=$?
check "run passes the script's exit status through (rc=$rc)" [ "$rc" -eq 3 ]
check "and records it" grep -q '"exit_status": 3' "$case/.pgf-run.json"
check "a failed run says FAILED with the exit status" grep -q "^pgf: Allfail FAILED (exit 3) after " "$TEST_TMP/err"
check "  and shows the tail of the newest log" \
    bash -c 'grep -q "last 20 lines of log.solver" "$1" && grep -qx "    FOAM FATAL ERROR: bad" "$1"' _ "$TEST_TMP/err"
rm -f "$case/log.solver"
printf '#!/bin/sh\nexit 4\n' >"$case/Allfail"
(cd / && penv run --prefix "$prefix" "$case" Allfail) >/dev/null 2>"$TEST_TMP/err"
check "  or says no log was written" grep -q "no log.\* written by this run" "$TEST_TMP/err"
printf '#!/bin/sh\nmkdir -p 0 0.5 0.25; echo done >log.porousGasificationFoam\n' >"$case/Allok" && chmod +x "$case/Allok"
(cd / && penv run --prefix "$prefix" "$case" Allok) >/dev/null 2>"$TEST_TMP/err"
check "a run with results gives the latest time, last log and the pgfView next step" \
    bash -c 'grep -q "^pgf: Allok OK in " "$1" && grep -q "latest time: 0.5$" "$1" &&
        grep -q "last log: *log.porousGasificationFoam" "$1" && grep -q "^pgf: next: .*pgfView \." "$1"' _ "$TEST_TMP/err"
rm -rf "$case/Allok" "$case/0" "$case/0.5" "$case/0.25" "$case/log.porousGasificationFoam"
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
out="$(penv build --yade --libs-only 2>/dev/null)"
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

# pgf where: settings, builds, records, run dir, the active build and its layer
penv where >"$TEST_TMP/where" 2>&1
check "where names both settings files" bash -c 'grep -qF "$2/.config/pgf/env.conf" "$1" && grep -qF "$3/.pgf.conf" "$1"' \
    _ "$TEST_TMP/where" "$home" "$tree"
check "where names the build store and its layer" grep -qE "^builds +/keep \[checkout $tree/.pgf.conf:[0-9]+\]" "$TEST_TMP/where"
check "where names the run records" grep -qxF "records   $home/.local/state/pgf/runs" "$TEST_TMP/where"
check "where names the run dir" grep -qE "^run dir +$TEST_TMP/runs \[checkout " "$TEST_TMP/where"
env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" PGF_BUILD=nosuchbuild \
    "$tree/pgf" where >"$TEST_TMP/where" 2>&1
check "where shows a terminal-selected build and that it was chosen there" \
    grep -qE "^build +nosuchbuild \[yade[01]; terminal: pgf use\] no build matches" "$TEST_TMP/where"
check "where fits on one screen (6 lines)" [ "$(wc -l <"$TEST_TMP/where")" -eq 6 ]

# pgf tools: one what / why / instead block per root tool
penv tools >"$TEST_TMP/tools" 2>&1
for t in pgf Allwmake Allwclean build.sh pgf-test; do
    check "pgf tools explains $t" bash -c 'awk -v t="$2" "\$0 == t { f = 1; next } f && /^$/ { exit } f" "$1" |
        grep -c -E "^  (what|why|instead): " | grep -qx 3' _ "$TEST_TMP/tools" "$t"
done

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
