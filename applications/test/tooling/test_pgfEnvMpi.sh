#!/bin/bash
# PGF_MPI=auto (#257 PR D) against stubbed interface lists (PGF_ENV_SYSFS_NET),
# with a fake bashrc on a copy of pgf-env: bridges or not exactly one interface
# up select loopback and say so in the banner; PGF_MPI=default leaves Open MPI
# alone; doctor reports the decision.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

bashrc="$FAKE/foam/etc/bashrc"
mkdir -p "${bashrc%/*}"
cat >"$bashrc" <<'EOF2'
export WM_PROJECT_DIR=/fake WM_PROJECT_VERSION=v2406 WM_OPTIONS=linux64GccDPInt32Opt
export WM_PROJECT_USER_DIR="$HOME/fake-user"
EOF2
tree="$TEST_TMP/tree"
mkdir -p "$tree/utilities/pgf-env"
cp "$PGF_ENV" "$PGF_ROOT/build.sh" "$tree/" && cp -r "$PGF_ROOT/utilities" "$tree/" || die "cannot copy pgf-env"

# net NAME:STATE ... — a stub /sys/class/net; prints its path.
net() {
    local d i
    d="$(mktemp -d "$TEST_TMP/net-XXXXXX")"
    mkdir "$d/lo" && echo unknown >"$d/lo/operstate"
    for i in "$@"; do mkdir "$d/${i%%:*}" && echo "${i#*:}" >"$d/${i%%:*}/operstate"; done
    printf '%s' "$d"
}
# probe SYSFS [VAR=value ...] [-- pgf-env args] — OMPI if_include, banner in $TEST_TMP/err.
probe() {
    local sys="$1" vars=() args=()
    shift
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do vars+=("$1"); shift; done
    [ "${1:-}" = "--" ] && { shift; args=("$@"); }
    env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC="$bashrc" \
        PGF_ENV_SYSFS_NET="$sys" "${vars[@]}" "$tree/pgf-env" --bare "${args[@]}" -- \
        printenv OMPI_MCA_btl_tcp_if_include 2>"$TEST_TMP/err"
}

one="$(net eth0:up docker0:down)"
check "one interface up (docker0 down): Open MPI left alone" [ -z "$(probe "$one")" ]
check "  and the banner says nothing about MPI" bash -c '! grep -q MPI "$1"' _ "$TEST_TMP/err"
check "--mpi loopback still forces lo" [ "$(probe "$one" -- --mpi loopback)" = lo ]

bridge="$(net eth0:up docker0:up)"
check "a docker bridge up selects loopback" [ "$(probe "$bridge")" = lo ]
check "  and the banner says why" grep -q "MPI loopback (auto: bridge/virtual interfaces up: docker0)" "$TEST_TMP/err"
check "two physical interfaces up select loopback" [ "$(probe "$(net eth0:up wlan0:up)")" = lo ]
check "only lo selects loopback" [ "$(probe "$(net)")" = lo ]
for v in virbr0 br-1234 veth9; do
    check "$v up selects loopback" [ "$(probe "$(net eth0:up "$v:up")")" = lo ]
done
check "PGF_MPI=default leaves Open MPI alone despite a bridge" [ -z "$(probe "$bridge" PGF_MPI=default)" ]
check "--mpi default does too" [ -z "$(probe "$bridge" -- --mpi default)" ]

env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC="$bashrc" \
    PGF_ENV_SYSFS_NET="$bridge" "$tree/pgf-env" doctor >"$TEST_TMP/doctor" 2>&1
check "doctor reports the auto decision and interfaces" \
    grep -q "MPI auto: loopback (bridge/virtual interfaces up: docker0)" "$TEST_TMP/doctor"
check "doctor checks the bashrc" grep -q "OK   OpenFOAM v2406 from $bashrc" "$TEST_TMP/doctor"
env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC=/nope \
    "$tree/pgf-env" doctor >"$TEST_TMP/doctor" 2>&1
rc=$?
check "doctor exits 1 and names the fix for a missing bashrc (rc=$rc)" \
    bash -c '[ "$1" -eq 1 ] && grep -q "FAIL no readable OpenFOAM bashrc.*fix: pgf-env init" "$2"' _ "$rc" "$TEST_TMP/doctor"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
