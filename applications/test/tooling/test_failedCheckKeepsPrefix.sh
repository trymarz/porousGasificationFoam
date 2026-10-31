#!/bin/bash
# A build refused by a prerequisite check leaves a complete prefix complete.
# Each variant gets a stand-in prefix whose metadata lacks DEM, so there is no
# cache hit, and YADE_TRUNK points at a tree without the Foam-Yade headers.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

export PGF_BUILD_ROOT="$TEST_TMP/builds"
export YADE_TRUNK="$TEST_TMP/no-foam-yade"
mkdir -p "$YADE_TRUNK"

# refused_keeps_marker <label> <build.sh args ...>
refused_keeps_marker() {
    local label="$1" prefix log="$TEST_TMP/$1.log" rc
    shift
    prefix="$(tree_prefix "$@")" || die "cannot resolve the $label prefix"
    mkdir -p "$prefix/bin" "$prefix/lib"
    printf '{"targets": ["fieldPorosityModel"]}\n' >"$prefix/.pgf-build.json"
    cp "$prefix/.pgf-build.json" "$TEST_TMP/$label.marker"

    isolated_build "$@" >"$log" 2>&1
    rc=$?
    check "$label: build is refused (rc=$rc)" [ "$rc" -ne 0 ]
    check "$label: refused by the Foam-Yade header check" grep -q "coupling headers not found" "$log"
    check "$label: no wmake target runs" bash -c '! grep -q "Building " "$1"' _ "$log"
    check "$label: .pgf-build.json is unchanged" cmp -s "$TEST_TMP/$label.marker" "$prefix/.pgf-build.json"
}

refused_keeps_marker yade1 --yade
refused_keeps_marker yade0-all --all
finish
