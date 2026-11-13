#!/bin/bash
# pgf settings layers (#257, #264): user file < checkout file < environment < CLI,
# a malformed or unknown line exits 2 naming file:line, and no config key can
# put a path under the default user dir back on PATH. Uses fake bashrcs and a
# copy of pgf, so neither OpenFOAM nor this checkout's .pgf-env.conf is touched.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/A/etc/bashrc}"
mkdir -p "$FAKE/A/etc" && : >"$FAKE/A/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

# fake_bashrc TAG — a bashrc that sets WM_PROJECT_VERSION=TAG and puts its own
#   user dir on PATH, as the real one does.
fake_bashrc() {
    mkdir -p "$FAKE/$1/etc"
    cat >"$FAKE/$1/etc/bashrc" <<EOF
export WM_PROJECT_DIR="$FAKE/$1" WM_PROJECT_VERSION="$1" WM_OPTIONS=linux64GccDPInt32Opt
export WM_PROJECT_USER_DIR="\$HOME/fake-user"
export PATH="\$WM_PROJECT_USER_DIR/platforms/\$WM_OPTIONS/bin:\$PATH"
EOF
    printf '%s' "$FAKE/$1/etc/bashrc"
}
A="$(fake_bashrc A)" B="$(fake_bashrc B)" C="$(fake_bashrc C)" D="$(fake_bashrc D)"

tree="$TEST_TMP/tree"
mkdir -p "$tree/utilities/pgf"
cp "$PGF" "$tree/" && cp "$PGF_ROOT"/utilities/pgf/* "$tree/utilities/pgf/" || die "cannot copy pgf"
home="$TEST_TMP/home"
user_conf="$home/.config/pgf/env.conf"
mkdir -p "${user_conf%/*}"

# penv [VAR=value ...] -- pgf args — a clean caller with only HOME/TMPDIR.
penv() {
    local vars=()
    while [ "$1" != "--" ]; do vars+=("$1"); shift; done
    shift
    env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" "${vars[@]}" "$tree/pgf" "$@"
}
# version [VAR=value ...] -- [pgf options] — WM_PROJECT_VERSION in that env.
version() {
    local vars=()
    while [ "$1" != "--" ]; do vars+=("$1"); shift; done
    shift
    penv "${vars[@]}" -- exec "$@" --bare -- printenv WM_PROJECT_VERSION 2>"$TEST_TMP/err"
}

penv -- exec --bare -- true 2>"$TEST_TMP/err"
rc=$?
check "no config, no env: exit 2 asking for PGF_FOAM_BASHRC (rc=$rc)" \
    bash -c '[ "$1" -eq 2 ] && grep -q PGF_FOAM_BASHRC "$2"' _ "$rc" "$TEST_TMP/err"
check "no config: the environment still works as before" [ "$(version PGF_FOAM_BASHRC="$A" --)" = A ]

echo "PGF_FOAM_BASHRC=$B" >"$user_conf"
check "user file alone selects B" [ "$(version --)" = B ]
penv -- status >"$TEST_TMP/status"
check "status names the user layer and line" grep -qE "^  PGF_FOAM_BASHRC +$B +\[user $user_conf:1\]" "$TEST_TMP/status"

printf '# checkout\n\nPGF_FOAM_BASHRC="%s"\nPGF_MPI=loopback\n' "$C" >"$tree/.pgf-env.conf"
check "checkout file overrides the user file" [ "$(version --)" = C ]
check "PGF_MPI=loopback from the checkout file restricts Open MPI to lo" \
    [ "$(penv -- exec --bare -- printenv OMPI_MCA_btl_tcp_if_include 2>/dev/null)" = lo ]
check "environment overrides both files" [ "$(version PGF_FOAM_BASHRC="$A" --)" = A ]
check "--foam-bashrc overrides the environment" \
    [ "$(version PGF_FOAM_BASHRC="$A" -- --foam-bashrc "$D")" = D ]
check "--set overrides the environment" \
    [ "$(version PGF_FOAM_BASHRC="$A" -- --set PGF_FOAM_BASHRC="$D")" = D ]
penv PGF_FOAM_BASHRC="$A" -- status --set PGF_RUN_ROOT=/x >"$TEST_TMP/status"
check "status reports env and cli layers" \
    bash -c 'grep -qE "^  PGF_FOAM_BASHRC .*\[env\]" "$1" && grep -qE "^  PGF_RUN_ROOT +/x +\[cli\]" "$1"' _ "$TEST_TMP/status"

printf 'PGF_FOAM_BASHRC=%s\n\nnot a setting\n' "$C" >"$tree/.pgf-env.conf"
version -- >/dev/null
rc=$?
check "malformed line exits 2 (rc=$rc)" [ "$rc" -eq 2 ]
check "the error names file:line" grep -qF "$tree/.pgf-env.conf:3:" "$TEST_TMP/err"

printf 'PATH=/evil\n' >"$tree/.pgf-env.conf"
version -- >/dev/null
rc=$?
check "an unknown key (PATH) exits 2 naming file:line (rc=$rc)" \
    bash -c '[ "$1" -eq 2 ] && grep -qF "$2:1: unknown key" "$3"' _ "$rc" "$tree/.pgf-env.conf" "$TEST_TMP/err"
printf 'PGF_YADE=yes\n' >"$tree/.pgf-env.conf"
version -- >/dev/null
check "a bad PGF_YADE value exits 2" [ $? -eq 2 ]

# Settings that name the default user dir must not put it back on PATH.
default="$home/fake-user"
printf 'PGF_YADE_LIBDIR=%s/lib\nPGF_RUN_ROOT=%s/run\n' "$default" "$default" >"$tree/.pgf-env.conf"
path="$(penv -- exec --bare -- printenv PATH 2>"$TEST_TMP/err")"
check "config pointing into the default user dir still runs" [ -n "$path" ]
check "PATH holds nothing under the default user dir $default" \
    bash -c '! tr ":" "\n" <<<"$1" | grep -qE "^$2(/|\$)"' _ "$path" "$default"

# PGF_BUILD selects a build per worktree or terminal, never per user.
printf 'PGF_FOAM_BASHRC=%s\nPGF_BUILD=abc123\n' "$A" >"$user_conf"
penv -- config get PGF_BUILD >/dev/null 2>"$TEST_TMP/err"
check "PGF_BUILD in the user file exits 2 naming the line" \
    bash -c '[ $? -eq 0 ]; grep -qF "$1:2: PGF_BUILD selects" "$2"' _ "$user_conf" "$TEST_TMP/err"
echo "PGF_FOAM_BASHRC=$A" >"$user_conf"
penv -- config set PGF_BUILD=abc123 >/dev/null 2>&1
check "config set PGF_BUILD without --checkout exits 2" [ $? -eq 2 ]
penv -- config set PGF_BUILD=abc123 --checkout 2>/dev/null
check "config set PGF_BUILD --checkout lands in the checkout file" grep -qx "PGF_BUILD=abc123" "$tree/.pgf-env.conf"
check "PGF_BUILD from the environment wins over the checkout file" \
    [ "$(penv PGF_BUILD=def456 -- config get PGF_BUILD)" = def456 ]

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
