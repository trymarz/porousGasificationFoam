#!/bin/bash
# check-mpi-bind — verify that the host MPI bind is correct and complete.
#
# Run with:
#   apptainer exec pgf-yade.sif check-mpi-bind                    # check in-container MPI
#   apptainer exec --bind HOST_MPI:/opt/openmpi pgf-yade.sif check-mpi-bind  # check after bind
#
# Exits 0 if all hard checks pass (WARNs do not fail).
# Exits 1 if any FAIL is detected.

set -euo pipefail

PASS=0; WARN=0; FAIL=0
ok()   { echo "  PASS  $*"; (( PASS++ )) || true; }
warn() { echo "  WARN  $*"; (( WARN++ )) || true; }
fail() { echo "  FAIL  $*"; (( FAIL++ )) || true; }

echo "=== pgf-yade MPI bind check ==="

# ── 1. Version: built-against vs live ─────────────────────────────────────────
echo ""
echo "-- 1. OpenMPI version --"
MANIFEST=/opt/pgf-manifest.txt
if [[ ! -f "$MANIFEST" ]]; then
    fail "No $MANIFEST — image may be incomplete."
else
    BUILT=$(awk '/^OpenMPI:/ {print $2}' "$MANIFEST")
    LIVE=$(ompi_info 2>/dev/null | awk '/Open MPI:/ {print $NF}' | head -1)

    if [[ -z "$LIVE" ]]; then
        fail "ompi_info failed — /opt/openmpi/bin not on PATH."
    else
        BUILT_MM=$(echo "$BUILT" | cut -d. -f1-2)
        LIVE_MM=$(echo "$LIVE"  | cut -d. -f1-2)

        if [[ "$BUILT_MM" == "$LIVE_MM" ]]; then
            ok "major.minor matches: built=${BUILT}  live=${LIVE}"
            # Exact match means the host bind may not have happened yet.
            if [[ "$BUILT" == "$LIVE" ]]; then
                warn "Live version equals built version — host MPI may not be bound."
                echo "         If this is an HPC node, add --bind HOST_MPI:/opt/openmpi to your run."
            fi
        else
            fail "VERSION MISMATCH: built against ${BUILT}, live is ${LIVE}."
            echo "         Rebuild: apptainer build --build-arg OMPI_VERSION=${LIVE} ..."
        fi
    fi
fi

# ── 2. Library linkage ────────────────────────────────────────────────────────
echo ""
echo "-- 2. Library linkage (ldd) --"
. /usr/lib/openfoam/openfoam2406/etc/bashrc 2>/dev/null || true

# Pstream
PSTREAM_SO="${FOAM_LIBBIN}/${FOAM_MPI}/libPstream.so"
if [[ -f "$PSTREAM_SO" ]]; then
    if ldd "$PSTREAM_SO" 2>/dev/null | grep -q '/opt/openmpi'; then
        ok "libPstream.so resolves libmpi from /opt/openmpi"
    else
        fail "libPstream.so does NOT link to /opt/openmpi."
        ldd "$PSTREAM_SO" 2>/dev/null | grep -i mpi | sed 's/^/         /' || true
    fi
else
    warn "libPstream.so not found at ${PSTREAM_SO} — skipping."
fi

# YADE binary
YADE_BIN=$(command -v yade 2>/dev/null || true)
if [[ -n "$YADE_BIN" ]]; then
    if ldd "$YADE_BIN" 2>/dev/null | grep -q '/opt/openmpi'; then
        ok "yade binary resolves libmpi from /opt/openmpi"
    else
        fail "yade does NOT link to /opt/openmpi."
        ldd "$YADE_BIN" 2>/dev/null | grep -i mpi | sed 's/^/         /' || true
    fi
else
    warn "yade not in PATH — skipping YADE ldd check."
fi

# ── 3. mpi4py rank communication ─────────────────────────────────────────────
echo ""
echo "-- 3. mpi4py rank communication --"
RANK_OUT=$(mpirun --allow-run-as-root -n 2 \
    python3 -c "from mpi4py import MPI; print(MPI.COMM_WORLD.Get_rank())" 2>&1) || true
R0=$(echo "$RANK_OUT" | grep -c '^0$' || true)
R1=$(echo "$RANK_OUT" | grep -c '^1$' || true)
if [[ "$R0" -ge 1 && "$R1" -ge 1 ]]; then
    ok "ranks 0 and 1 both reported distinct values"
else
    fail "ranks did not report distinct values — collective communication broken."
    echo "$RANK_OUT" | sed 's/^/         /' || true
fi

# ── 4. Transport plugins ──────────────────────────────────────────────────────
echo ""
echo "-- 4. MPI transport availability --"
TRANSPORTS=$(ompi_info --all 2>/dev/null \
    | awk '/^\s+MCA (btl|mtl|pml):/ {print $3}' | sort -u | tr '\n' ' ' || true)
if [[ -n "$TRANSPORTS" ]]; then
    ok "Available: ${TRANSPORTS}"
else
    warn "Could not enumerate transports — ompi_info may be missing."
fi
if ! ompi_info --all 2>/dev/null | grep -qiE 'ucx|openib|ofi'; then
    warn "No high-perf transport (UCX/OFI/openib) detected."
    echo "         On HPC you may also need to bind UCX/ibverbs paths."
    echo "         See the README Container section for example --bind flags."
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "=== Result: ${PASS} PASS, ${WARN} WARN, ${FAIL} FAIL ==="
[[ "$FAIL" -eq 0 ]]
