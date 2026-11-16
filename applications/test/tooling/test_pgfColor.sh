#!/bin/bash
# Colour on a terminal by default (#287): pgf decides once, before any output,
# so lines built inside $(...) are coloured too. Runs pgf under a pty
# (script(1)), not only with --color=always. Fake bashrc; nothing is built.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

command -v script >/dev/null || die "script (util-linux) is needed for a pty"
home="$TEST_TMP/home"
mkdir -p "$home"
bashrc="$FAKE/foam/etc/bashrc"
mkdir -p "${bashrc%/*}"
echo 'export WM_PROJECT_DIR=/fake WM_PROJECT_VERSION=v2406 WM_OPTIONS=linux64 WM_PROJECT_USER_DIR="$HOME/u"' >"$bashrc"

# pty CMD — CMD under a pseudo-terminal (stdout and stderr both the pty).
pty() {
    env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" TERM=xterm-256color "${extra[@]}" \
        script -qec "$1" /dev/null </dev/null
}
esc=$'\e['
extra=()

pty "$(printf '%q ' "$PGF" help)" >"$TEST_TMP/help"
check "pgf help on a terminal has bold headings by default" grep -qF "${esc}1mUsage:" "$TEST_TMP/help"
pty "$(printf '%q ' "$PGF" help setup)" >"$TEST_TMP/help"
check "  and so does a command's page, defaults dimmed" \
    bash -c 'grep -qF "${2}1mOptions:" "$1" && grep -qF "${2}2m(default)" "$1"' _ "$TEST_TMP/help" "$esc"

pty "$(printf '%q ' "$PGF" setup --user --yes --no-link --no-completion --foam-bashrc "$bashrc")" >"$TEST_TMP/setup"
check "setup's step labels are coloured on a terminal" grep -qF "${esc}32mOK" "$TEST_TMP/setup"
check "  and its arrows" grep -qF "${esc}36m→" "$TEST_TMP/setup"
check "  and it wrote the user file, not this checkout's" \
    bash -c 'grep -q "^PGF_FOAM_BASHRC=" "$1/.config/pgf/env.conf"' _ "$home"

extra=(NO_COLOR=1)
pty "$(printf '%q ' "$PGF" help)" >"$TEST_TMP/help"
check "NO_COLOR turns the default off" bash -c '! grep -qF "$2" "$1"' _ "$TEST_TMP/help" "$esc"
extra=(TERM=dumb)
pty "$(printf '%q ' "$PGF" help)" >"$TEST_TMP/help"
check "so does TERM=dumb" bash -c '! grep -qF "$2" "$1"' _ "$TEST_TMP/help" "$esc"
extra=()
pty "$(printf '%q ' "$PGF" --color=never help)" >"$TEST_TMP/help"
check "--color=never wins on a terminal" bash -c '! grep -qF "$2" "$1"' _ "$TEST_TMP/help" "$esc"
pty "$(printf '%q ' "$PGF" help) | cat" >"$TEST_TMP/help"
check "a pipe gets no colour, even with stderr on the terminal" bash -c '! grep -qF "$2" "$1"' _ "$TEST_TMP/help" "$esc"
pty "$(printf '%q ' "$PGF" help) 2>/dev/null" >"$TEST_TMP/help"
check "nor does a terminal stdout with stderr redirected" bash -c '! grep -qF "$2" "$1"' _ "$TEST_TMP/help" "$esc"

[ "$TEST_FAILURES" -eq 0 ] || cat "$TEST_TMP/help"
finish
