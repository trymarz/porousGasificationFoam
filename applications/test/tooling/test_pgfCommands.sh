#!/bin/bash
# pgf setup / config / shell / run / build and the pgf-env alias (#257 B,
# #264 PR 1), with a fake bashrc, prefix and yade on a copy of pgf, so nothing
# is built and neither the real user config nor this checkout's .pgf-env.conf is touched.

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
cp "$PGF" "$PGF_ENV" "$tree/" && cp "$PGF_ROOT"/utilities/pgf/* "$tree/utilities/pgf/" || die "cannot copy pgf"

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
# alias ARGS — the same caller through the deprecated pgf-env.
alias_env() {
    env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" \
        ${PGF_ENV_QUIET:+PGF_ENV_QUIET=$PGF_ENV_QUIET} \
        "$tree/pgf-env" "$@"
}

# setup writes a file that status reads back.
penv setup --checkout --yes --no-link --foam-bashrc "$bashrc" --set PGF_RUN_ROOT="$TEST_TMP/runs" \
    >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?
check "setup --checkout --yes exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "setup --no-link made no link" [ ! -e "$home/.local/bin/pgf" ]
check "setup wrote the checkout file" grep -qx "PGF_FOAM_BASHRC=$bashrc" "$tree/.pgf-env.conf"
check "setup found the suffixed yade on the caller's PATH" \
    grep -qx "YADE_BIN=$yadedir/yade-2026.1" "$tree/.pgf-env.conf"
penv status >"$TEST_TMP/status" 2>&1
check "status reads PGF_FOAM_BASHRC back from the checkout file" \
    grep -qE "^  PGF_FOAM_BASHRC +$bashrc +\[checkout " "$TEST_TMP/status"
printf 'PGF_BUILD_ROOT=/keep\n' >>"$tree/.pgf-env.conf"
penv setup --checkout --yes --no-link --set PGF_MPI=loopback >/dev/null 2>&1
check "a second setup keeps existing keys and adds the new one" \
    bash -c 'grep -qx "PGF_BUILD_ROOT=/keep" "$1" && grep -qx "PGF_MPI=loopback" "$1" && grep -qx "PGF_FOAM_BASHRC=$2" "$1"' \
    _ "$tree/.pgf-env.conf" "$bashrc"
penv setup --user --yes --foam-bashrc "$bashrc" >/dev/null 2>&1
check "setup --user writes ~/.config/pgf/env.conf" grep -qx "PGF_FOAM_BASHRC=$bashrc" "$home/.config/pgf/env.conf"
check "  and links ~/.local/bin/pgf to this checkout" \
    [ "$(readlink -f "$home/.local/bin/pgf")" = "$(readlink -f "$tree/pgf")" ]

# config
check "config get reads the resolved value" [ "$(penv config get PGF_MPI)" = loopback ]
penv config get PGF_YADE_LIBDIR >/dev/null 2>&1
check "config get of an unset key exits 1" [ $? -eq 1 ]
penv config get NOPE >/dev/null 2>&1
check "config get of an unknown key exits 2" [ $? -eq 2 ]
penv config set PGF_YADE_LIBDIR=/opt/libs --checkout 2>/dev/null
check "config set --checkout writes the checkout file" grep -qx "PGF_YADE_LIBDIR=/opt/libs" "$tree/.pgf-env.conf"
check "  and keeps the other keys" grep -qx "PGF_MPI=loopback" "$tree/.pgf-env.conf"
penv config set PGF_MPI=sometimes 2>/dev/null
check "config set rejects a bad value with exit 2" [ $? -eq 2 ]
penv config unset PGF_YADE_LIBDIR --checkout 2>/dev/null
check "config unset removes the key" bash -c '! grep -q PGF_YADE_LIBDIR "$1"' _ "$tree/.pgf-env.conf"
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
printf '#!/bin/sh\nexit 3\n' >"$case/Allfail" && chmod +x "$case/Allfail"
(cd / && penv run --prefix "$prefix" "$case" Allfail) >/dev/null 2>&1
rc=$?
check "run passes the script's exit status through (rc=$rc)" [ "$rc" -eq 3 ]
check "and records it" grep -q '"exit_status": 3' "$case/.pgf-run.json"
mkdir -p "$TEST_TMP/broot"
(cd / && env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" PGF_BUILD_ROOT="$TEST_TMP/broot" \
    "$tree/pgf" run --prefix "$prefix" "$case" Allclean) >/dev/null 2>&1
check "runs.jsonl under the build root gets the run" \
    grep -q "\"case\": \"$(realpath "$case")\"" "$TEST_TMP/broot/.pgf/runs.jsonl"
rm -f "$case/Allfail" "$case/.pgf-run.json"
rm -f "$case/ran.txt"
(cd "$TEST_TMP/cases" && penv run --prefix "$prefix" plain) >/dev/null 2>&1
check "a relative case dir resolves from the caller and Allrun is the default" [ -f "$case/ran.txt" ]

dem="$TEST_TMP/cases/dem"
cp -r "$case" "$dem" && rm -f "$dem/ran.txt"
printf 'FoamFile { object yadeProperties; }\nactive          true;\n' >"$dem/constant/yadeProperties"
(cd / && penv run --prefix "$prefix" "$dem") >/dev/null 2>"$TEST_TMP/err"
check "an active yadeProperties selects --yade (yade shim on PATH)" grep -q '/pgf-env-bin/yade$' "$dem/ran.txt"
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
penv setup --link-only --force >/dev/null 2>&1
check "setup --link-only --force replaces it" \
    [ "$(readlink -f "$home/.local/bin/pgf")" = "$(readlink -f "$tree/pgf")" ]

# build hands --isolated (and --yade) to build.sh under exec --bare
printf '#!/bin/sh\necho "build.sh $*"; echo "user=$WM_PROJECT_USER_DIR"\n' >"$tree/build.sh"
chmod +x "$tree/build.sh"
out="$(penv build --yade --libs-only 2>/dev/null)"
check "build runs build.sh --isolated --yade with the extra options" grep -qx "build.sh --isolated --yade --libs-only" <<<"$out"
check "  in a bare env (user dir is the build root)" grep -qx "user=/keep" <<<"$out"

# the deprecated alias
out="$(alias_env --prefix "$prefix" -- bash -c "command -v porousGasificationFoam" 2>"$TEST_TMP/err")"
check "pgf-env [opts] -- CMD runs CMD like pgf exec" grep -qx "$prefix/bin/porousGasificationFoam" <<<"$out"
check "  and says it is deprecated" grep -q "pgf-env: deprecated; use pgf" "$TEST_TMP/err"
alias_env status >"$TEST_TMP/status" 2>/dev/null
check "pgf-env status still prints the settings" grep -qE "^  PGF_MPI +loopback +\[checkout " "$TEST_TMP/status"
alias_env init --checkout --yes --set PGF_BUILD_ROOT=/alias >/dev/null 2>&1
check "pgf-env init writes settings without touching the link" \
    bash -c 'grep -qx "PGF_BUILD_ROOT=/alias" "$1" && [ "$(readlink -f "$2")" = "$(readlink -f "$3")" ]' \
    _ "$tree/.pgf-env.conf" "$home/.local/bin/pgf" "$tree/pgf"
rm -f "$case/ran.txt"
out="$(PGF_ENV_QUIET=1 alias_env run --prefix "$prefix" "$case" Allclean 2>"$TEST_TMP/err")"
check "pgf-env run still runs a case; PGF_ENV_QUIET=1 drops the note" \
    bash -c '[ -f "$1/ran.txt" ] && ! grep -q deprecated "$2"' _ "$case" "$TEST_TMP/err"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
