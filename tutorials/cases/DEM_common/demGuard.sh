# demGuard.sh — sourced by the DEM tutorials' Allrun (#257). Stops before any
# work, with exit 2 and the fix, when this shell lacks the WITH_YADE solver or
# yade. POSIX sh: some Allruns are #!/bin/sh.

pgf_dem_guard_fail() {
    echo "Allrun: $1" >&2
    echo "Allrun: run this via: pgf-env run $PWD   (or inside: pgf-env shell --yade)" >&2
    exit 2
}
if ! command -v porousGasificationFoam >/dev/null 2>&1; then
    pgf_dem_guard_fail "porousGasificationFoam is not on PATH"
fi
if ! command -v yade >/dev/null 2>&1; then
    pgf_dem_guard_fail "yade is not on PATH"
fi
if ! ldd "$(command -v porousGasificationFoam)" 2>/dev/null | grep -q 'libYadeFoam'; then
    pgf_dem_guard_fail "porousGasificationFoam is not the --yade build (no DEM coupling)"
fi
