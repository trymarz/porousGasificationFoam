#!/bin/bash
# pgf-selftest — in-container smoke tests for the pgf-yade image.
#
# Run with:
#   apptainer exec pgf-yade.sif pgf-selftest        # inside the container, no host MPI
#   apptainer test pgf-yade.sif                     # same (uses the %test section)
#
# Tests only what can be checked without a host MPI bind:
#   1. solver binary loads
#   2. YADE binary loads
#   3. mpi4py imports
#   4. libPstream.so links to /opt/openmpi
#   5. in-container mpirun -n 2 works
#
# For host-bind and multi-node tests use check-mpi-bind, or see the verification
# ladder in the README Container section.

set -euo pipefail

PASS=0; FAIL=0
ok()   { echo "  [PASS] $*"; (( PASS++ )) || true; }
fail() { echo "  [FAIL] $*"; (( FAIL++ )) || true; return 1; }

. /usr/lib/openfoam/openfoam2406/etc/bashrc 2>/dev/null || true
. /opt/pgf-src/porousGasificationMediaDirectories 2>/dev/null || true
export PATH=/opt/yade/bin:/opt/openmpi/bin:$PATH

echo "=== pgf-yade in-container self-test ==="
echo ""

echo "-- 1/5  solver binary --"
if porousGasificationFoam -help > /dev/null 2>&1; then
    ok "porousGasificationFoam -help"
else
    fail "porousGasificationFoam -help returned non-zero"
fi

echo "-- 2/5  YADE binary --"
if yade --version > /dev/null 2>&1; then
    ok "yade --version"
else
    fail "yade --version failed"
fi

echo "-- 3/5  mpi4py import --"
if python3 -c "import mpi4py" 2>/dev/null; then
    ok "import mpi4py"
else
    fail "import mpi4py failed"
fi

echo "-- 4/5  libPstream.so links openmpi --"
PSTREAM_SO="${FOAM_LIBBIN}/${FOAM_MPI}/libPstream.so"
if [[ -f "$PSTREAM_SO" ]] && ldd "$PSTREAM_SO" 2>/dev/null | grep -q '/opt/openmpi'; then
    ok "libPstream.so → /opt/openmpi"
else
    fail "libPstream.so not found or does not link /opt/openmpi (path: ${PSTREAM_SO})"
fi

echo "-- 5/5  in-container mpirun -n 2 --"
if mpirun --allow-run-as-root -n 2 hostname > /dev/null 2>&1; then
    ok "mpirun -n 2 hostname"
else
    fail "in-container mpirun -n 2 failed"
fi

echo ""
echo "=== ${PASS}/5 passed ==="
[[ "$FAIL" -eq 0 ]]
