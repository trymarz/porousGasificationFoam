#!/bin/bash
# toolingLib.sh — shared helpers for the build/environment tooling tests.
#
# Source-only. Each test_*.sh in this directory sources it and runs alone:
#   PGF_FOAM_BASHRC=/path/to/OpenFOAM-v2406/etc/bashrc \
#   PGF_BUILD_ROOT=/scratch/pgf-builds  applications/test/tooling/test_<name>.sh
# PGF_BUILD_ROOT defaults to ${TMPDIR:-/tmp}/pgf-tooling-builds, so a test never
# builds into the default $WM_PROJECT_USER_DIR/pgf-builds. Exit 0 PASS, 1 FAIL,
# 2 ERROR (could not set the test up).

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "toolingLib.sh is source-only" >&2
    exit 2
fi

set -u

TOOLING_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PGF_ROOT="$(cd "$TOOLING_DIR/../../.." && pwd -P)"
PGF_ENV="$PGF_ROOT/pgf-env"
BUILD_SH="$PGF_ROOT/build.sh"

export PGF_BUILD_ROOT="${PGF_BUILD_ROOT:-${TMPDIR:-/tmp}/pgf-tooling-builds}"
if [ -z "${PGF_FOAM_BASHRC:-}" ] && [ -n "${WM_PROJECT_DIR:-}" ]; then
    export PGF_FOAM_BASHRC="$WM_PROJECT_DIR/etc/bashrc"
fi
[ -r "${PGF_FOAM_BASHRC:-}" ] || {
    echo "ERROR: set PGF_FOAM_BASHRC (or WM_PROJECT_DIR) to OpenFOAM's etc/bashrc" >&2
    exit 2
}

TEST_NAME="$(basename "$0" .sh)"
TEST_TMP="$(mktemp -d "${TMPDIR:-/tmp}/pgf-$TEST_NAME-XXXXXX")" || exit 2
TEST_FAILURES=0
trap 'rm -rf "$TEST_TMP"' EXIT

pass() { echo "  PASS  $*"; }
fail() { echo "  FAIL  $*"; TEST_FAILURES=$((TEST_FAILURES + 1)); }
die()  { echo "  ERROR $*" >&2; exit 2; }

# check <description> <command ...> — run a predicate and record the result.
check() {
    local what="$1"
    shift
    if "$@"; then pass "$what"; else fail "$what"; fi
}

finish() {
    if [ "$TEST_FAILURES" -eq 0 ]; then
        echo "$TEST_NAME: PASS"
        exit 0
    fi
    echo "$TEST_NAME: FAIL ($TEST_FAILURES check(s))"
    exit 1
}

# tree_sums <dir ...> — sha256 of every regular file below the dirs, sorted.
tree_sums() {
    local d
    for d in "$@"; do
        [ -d "$d" ] && find "$d" -type f -print0
    done | LC_ALL=C sort -z | xargs -0 -r sha256sum
}

# isolated_build [build.sh args] — build.sh --isolated under pgf-env --bare.
isolated_build() {
    "$PGF_ENV" --bare -- "$BUILD_SH" --isolated "$@"
}

tree_prefix() {
    "$PGF_ENV" --bare -- "$BUILD_SH" --print-prefix "$@" 2>/dev/null
}

default_user_dir() {
    "$PGF_ENV" --bare -- printenv PGF_DEFAULT_USER_DIR 2>/dev/null
}
