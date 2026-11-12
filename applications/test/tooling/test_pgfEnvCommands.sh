#!/bin/bash
# pgf-env init / shell / run / install-link (#257 PR B), with a fake bashrc,
# a fake prefix and a fake yade on a copy of pgf-env, so nothing is built and
# neither the real user config nor this checkout's .pgf-env.conf is touched.

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
mkdir -p "$tree/utilities/pgf-env"
cp "$PGF_ENV" "$tree/" && cp "$PGF_ROOT"/utilities/pgf-env/* "$tree/utilities/pgf-env/" || die "cannot copy pgf-env"

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

# penv ARGS — a clean caller (HOME, TMPDIR, a PATH with only a suffixed yade).
penv() {
    env -i PATH="$yadedir:/usr/bin:/bin" HOME="$home" TMPDIR="$TEST_TMP" YADE_TRUNK="$trunk" \
        "$tree/pgf-env" "$@"
}

# init writes a file that status reads back.
penv init --checkout --yes --foam-bashrc "$bashrc" --set PGF_RUN_ROOT="$TEST_TMP/runs" \
    >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?
check "init --checkout --yes exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "init wrote the checkout file" grep -qx "PGF_FOAM_BASHRC=$bashrc" "$tree/.pgf-env.conf"
check "init found the suffixed yade on the caller's PATH" \
    grep -qx "YADE_BIN=$yadedir/yade-2026.1" "$tree/.pgf-env.conf"
penv status >"$TEST_TMP/status" 2>&1
check "status reads PGF_FOAM_BASHRC back from the checkout file" \
    grep -qE "^  PGF_FOAM_BASHRC +$bashrc +\[checkout " "$TEST_TMP/status"
printf 'PGF_BUILD_ROOT=/keep\n' >>"$tree/.pgf-env.conf"
penv init --checkout --yes --set PGF_MPI=loopback >/dev/null 2>&1
check "a second init keeps existing keys and adds the new one" \
    bash -c 'grep -qx "PGF_BUILD_ROOT=/keep" "$1" && grep -qx "PGF_MPI=loopback" "$1" && grep -qx "PGF_FOAM_BASHRC=$2" "$1"' \
    _ "$tree/.pgf-env.conf" "$bashrc"
penv init --user --yes --foam-bashrc "$bashrc" >/dev/null 2>&1
check "init --user writes ~/.config/pgf/env.conf" grep -qx "PGF_FOAM_BASHRC=$bashrc" "$home/.config/pgf/env.conf"

# shell
out="$(penv shell --prefix "$prefix" -c 'command -v porousGasificationFoam; echo "active=$PGF_ENV_ACTIVE"' 2>"$TEST_TMP/err")"
check "shell -c resolves the solver into the prefix" grep -qx "$prefix/bin/porousGasificationFoam" <<<"$out"
check "shell -c runs with PGF_ENV_ACTIVE=1" grep -qx "active=1" <<<"$out"
out="$(echo 'echo "PS1=$PS1"; exit' | penv shell --prefix "$prefix" 2>/dev/null)"
check "interactive shell sets the (pgf:<key>) prompt marker" grep -qF "PS1=(pgf:prefix)" <<<"$out"

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

# install-link
penv install-link >/dev/null 2>"$TEST_TMP/err"
check "install-link creates ~/.local/bin/pgf-env" \
    [ "$(readlink -f "$home/.local/bin/pgf-env")" = "$(readlink -f "$tree/pgf-env")" ]
ln -sfn /elsewhere/pgf-env "$home/.local/bin/pgf-env"
penv install-link >/dev/null 2>&1
check "install-link refuses to replace another link without --force" [ $? -eq 2 ]
penv install-link --force >/dev/null 2>&1
check "install-link --force replaces it" \
    [ "$(readlink -f "$home/.local/bin/pgf-env")" = "$(readlink -f "$tree/pgf-env")" ]

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
