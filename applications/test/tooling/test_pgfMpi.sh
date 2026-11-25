#!/bin/bash
# PGF_MPI=auto (#257 PR D) against stubbed interface lists (PGF_ENV_SYSFS_NET),
# with a fake bashrc on a copy of pgf: bridges or not exactly one interface
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
export WM_PROJECT_DIR=/fake WM_PROJECT_VERSION=v2606 WM_OPTIONS=linux64GccDPInt32Opt
export WM_PROJECT_USER_DIR="$HOME/fake-user"
EOF2
tree="$TEST_TMP/tree"
mkdir -p "$tree/utilities"
cp "$PGF" "$tree/" && cp -r "$PGF_ROOT/utilities" "$tree/" || die "cannot copy pgf"

# net NAME:STATE ... — a stub /sys/class/net; prints its path.
net() {
    local d i
    d="$(mktemp -d "$TEST_TMP/net-XXXXXX")"
    mkdir "$d/lo" && echo unknown >"$d/lo/operstate"
    for i in "$@"; do mkdir "$d/${i%%:*}" && echo "${i#*:}" >"$d/${i%%:*}/operstate"; done
    printf '%s' "$d"
}
# probe SYSFS [VAR=value ...] [-- pgf options] — OMPI if_include, banner in $TEST_TMP/err.
probe() {
    local sys="$1" vars=() args=()
    shift
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do vars+=("$1"); shift; done
    [ "${1:-}" = "--" ] && { shift; args=("$@"); }
    env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC="$bashrc" \
        PGF_ENV_SYSFS_NET="$sys" "${vars[@]}" "$tree/pgf" exec --bare "${args[@]}" -- \
        printenv OMPI_MCA_btl_tcp_if_include 2>"$TEST_TMP/err"
}

one="$(net eth0:up docker0:down)"
check "one interface up (docker0 down): Open MPI left alone" [ -z "$(probe "$one")" ]
check "  and the banner says nothing about MPI" bash -c '! grep -q MPI "$1"' _ "$TEST_TMP/err"
check "--mpi loopback still forces lo" [ "$(probe "$one" -- --mpi loopback)" = lo ]

bridge="$(net eth0:up docker0:up)"
check "a docker bridge up selects loopback" [ "$(probe "$bridge")" = lo ]
check "  and the banner says why, in words" grep -qx "  INFO MPI: local only (lo), because bridge/virtual interfaces up: docker0" "$TEST_TMP/err"
# A serial run (no system/decomposeParDict) keeps the setting but not the line (S4-16).
for c in serial parallel; do
    mkdir -p "$TEST_TMP/$c/system" && printf '#!/bin/sh\nprintenv OMPI_MCA_btl_tcp_if_include\n' >"$TEST_TMP/$c/Allrun" &&
        chmod +x "$TEST_TMP/$c/Allrun"
done
: >"$TEST_TMP/parallel/system/decomposeParDict"
rcase() {
    env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC="$bashrc" \
        PGF_ENV_SYSFS_NET="$bridge" "$tree/pgf" run --bare "$TEST_TMP/$1" 2>"$TEST_TMP/err"
}
check "a serial run still gets loopback" [ "$(rcase serial)" = lo ]
check "  but no MPI line" bash -c '! grep -q "MPI:" "$1"' _ "$TEST_TMP/err"
check "a parallel run (decomposeParDict) gets loopback" [ "$(rcase parallel)" = lo ]
check "  and the MPI line" grep -q "INFO MPI: local only (lo)" "$TEST_TMP/err"
check "two physical interfaces up select loopback" [ "$(probe "$(net eth0:up wlan0:up)")" = lo ]
check "only lo selects loopback" [ "$(probe "$(net)")" = lo ]
for v in virbr0 br-1234 veth9; do
    check "$v up selects loopback" [ "$(probe "$(net eth0:up "$v:up")")" = lo ]
done
check "PGF_MPI=default leaves Open MPI alone despite a bridge" [ -z "$(probe "$bridge" PGF_MPI=default)" ]
check "--mpi default does too" [ -z "$(probe "$bridge" -- --mpi default)" ]

env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC="$bashrc" \
    PGF_ENV_SYSFS_NET="$bridge" "$tree/pgf" doctor >"$TEST_TMP/doctor" 2>&1
check "doctor reports the auto decision and interfaces under MPI" \
    bash -c 'grep -A1 -x MPI "$1" | grep -qx "  OK   auto: local only (lo), because bridge/virtual interfaces up: docker0"' _ "$TEST_TMP/doctor"
check "doctor checks the bashrc under OpenFOAM" bash -c 'grep -A1 -x OpenFOAM "$1" | grep -qx "  OK   v2606, $2"' _ "$TEST_TMP/doctor" "$bashrc"
check "doctor opens with one line on what it checks, and no settings block" \
    bash -c 'head -1 "$1" | grep -q "^pgf doctor: read-only checks" && ! grep -q "^settings:" "$1"' _ "$TEST_TMP/doctor"
check "  groups the checks" \
    [ "$(grep -xE "OpenFOAM|build|yade and DEM|MPI|PATH" "$TEST_TMP/doctor" | tr '\n' ' ')" = "OpenFOAM build yade and DEM MPI PATH " ]
check "  and ends with a verdict that names what DEM needs" \
    bash -c 'tail -1 "$1" | grep -q "^verdict: .*DEM needs: yade (pgf config set YADE_BIN=<yade>)"' _ "$TEST_TMP/doctor"
env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" TMPDIR="$TEST_TMP" PGF_FOAM_BASHRC=/nope \
    "$tree/pgf" doctor >"$TEST_TMP/doctor" 2>&1
rc=$?
check "doctor exits 1 and names the fix for a missing bashrc, under it (rc=$rc)" \
    bash -c '[ "$1" -eq 1 ] && grep -A1 "FAIL no readable OpenFOAM bashrc" "$2" | grep -q "^       fix: ./pgf setup"' _ "$rc" "$TEST_TMP/doctor"
check "  and the verdict says not ready" bash -c 'tail -1 "$1" | grep -q "^verdict: not ready"' _ "$TEST_TMP/doctor"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish
