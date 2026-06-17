#!/bin/sh
# poster-extract.sh — extract poster data from a finished posterDemo run.
# Run from the case directory after the solver exits.
# Usage:  ./poster-extract.sh
#         ./poster-extract.sh  (re-run after any solver re-start to update data)

cd "${0%/*}" || exit

echo "╔═══════════════════════════════════════════════╗"
echo "║  poster-extract.sh — poster data extraction  ║"
echo "╚═══════════════════════════════════════════════╝"

# ── 1. Merge parallel domains ───────────────────────
echo ""
echo "── 1/6  reconstructPar (merge processor dirs) ──"
if command -v reconstructPar >/dev/null 2>&1; then
    reconstructPar -noZero 2>&1 | tee log.reconstructPar
    ls -d [0-9]*/ 2>/dev/null | wc -l | xargs echo "  → reconstructed write times:"
else
    echo "  → reconstructPar not on PATH — skip (fields available per-processor)"
fi

# ── 2. Full time-series report ──────────────────────
echo ""
echo "── 2/6  poster-report --all (time-series table) ──"
if [ -x ./poster-report ]; then
    ./poster-report --all | tee poster_summary.txt
else
    uv run python ./poster-report --all | tee poster_summary.txt
fi

echo ""
echo "── 3/6  poster-report --csv (for plotting) ──"
if [ -x ./poster-report ]; then
    ./poster-report --csv > poster_metrics.csv
else
    uv run python ./poster-report --csv > poster_metrics.csv
fi
echo "  → wrote poster_metrics.csv"

# ── 4. Poster snapshot details ──────────────────────
echo ""
echo "── 4/6  poster snapshots (t=0.67, 1.33, 2.00) ──"
mkdir -p snapshots
for t in 0.67 1.33 2.0; do
    if [ -x ./poster-report ]; then
        ./poster-report -t "$t" > "snapshots/t${t}.txt"
    else
        uv run python ./poster-report -t "$t" > "snapshots/t${t}.txt"
    fi
    echo "  → snapshots/t${t}.txt"
done

# ── 5. Sphere shrinkage time series ─────────────────
echo ""
echo "── 5/6  sphere radius tracking ──"
uv run python extract_sphere_timeseries.py --csv --all > sphere_stats.csv 2>&1
echo "  → sphere_stats.csv (all-sphere stats per frame)"
uv run python extract_sphere_timeseries.py --csv      > tracked_spheres.csv 2>&1
echo "  → tracked_spheres.csv (3 tracked spheres at 3 heights)"

# ── 6. Copy key VTK files for ParaView ──────────────
echo ""
echo "── 6/6  key sphere VTK files ──"
mkdir -p key_vtk_frames
for t in 0.6700 1.3300 2.0000; do
    f="spheres/spheres-rank0-${t}.vtp"
    if [ -f "$f" ]; then
        cp "$f" key_vtk_frames/
        echo "  → key_vtk_frames/spheres-rank0-${t}.vtp"
    else
        echo "  → (no spheres/${t}.vtp — may have ended early)"
    fi
done

# ── Done ────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║  Extraction complete                         ║"
echo "║                                              ║"
echo "║  Data summary:                               ║"
echo "║    poster_summary.txt     — full time-series  ║"
echo "║    poster_metrics.csv     — CSV for plotting  ║"
echo "║    snapshots/*            — 3 poster times    ║"
echo "║    sphere_stats.csv       — shrinkage stats   ║"
echo "║    tracked_spheres.csv    — 3 spheres r(t)    ║"
echo "║    key_vtk_frames/*       — sphere geometry   ║"
echo "║    log.reconstructPar     — reconstruction    ║"
echo "╚═══════════════════════════════════════════════╝"
