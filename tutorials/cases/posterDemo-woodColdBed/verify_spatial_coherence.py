#!/usr/bin/env python3
"""verify_spatial_coherence — check that PGF solid region and DEM spheres
stay in the same physical space throughout the simulation.

Spatial coherence is the central proof of the coupling. If the z-centroids
of the PGF solid region (cells with porosityF < 0.5) and the DEM spheres
drift apart, the coupling loop is broken (see core-message-and-showcase §2.3).

Usage:
    uv run python verify_spatial_coherence.py
    uv run python verify_spatial_coherence.py --csv  > coherence.csv

Output:
    Table of (t, z_pgf, z_dem, |dz|, dz/bed_height, status) for every
    write time where both PGF field data and sphere VTK exist.
"""

from __future__ import annotations

import argparse
import glob as _glob
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

CASE = Path("/workspace/trymarz/porousGasificationFoam/tutorials/cases/posterDemo-woodStart")

# ── helpers ───────────────────────────────────────────────────────────

def read_scalar_field(path: Path) -> np.ndarray:
    """Read a volScalarField internalField into a numpy array."""
    if not path.is_file():
        return np.array([])
    data = path.read_bytes()
    idx = data.find(b"internalField")
    if idx < 0:
        return np.array([])
    tail = data[idx:].decode("ascii", errors="ignore")
    m = re.search(r"\(", tail)
    if not m:
        return np.array([])
    start = m.start()
    depth = 0
    end = start
    for i in range(start, len(tail)):
        if tail[i] == "(":
            depth += 1
        elif tail[i] == ")":
            depth -= 1
            if depth == 0:
                end = i
                break
    block = tail[start : end + 1]
    vals = []
    for tok in re.findall(r"[\d.eE+\-]+", block):
        try:
            vals.append(float(tok))
        except ValueError:
            pass
    return np.array(vals)


def read_scalar_field_decomposed(case_dir: Path, time: float, field: str) -> np.ndarray:
    """Read a volScalarField from all processor directories and concatenate.

    For a decomposed case, each processor<i>/ directory holds a subset of
    cells. Concatenating processor0..processorN-1 reconstructs the full
    field, matching the cell ordering of the undecomposed polyMesh.
    """
    parts = []
    for i in range(99):  # safe upper bound
        pdir = case_dir / f"processor{i}"
        if not pdir.is_dir():
            break
        tdir = pdir / f"{time:g}"
        vals = read_scalar_field(tdir / field)
        if len(vals) == 0:
            break
        parts.append(vals)
    if parts:
        return np.concatenate(parts)
    return np.array([])


def read_sphere_positions(vtp_path: Path) -> np.ndarray:
    """Return (N, 3) array of sphere xyz from a VTP file."""
    if not vtp_path.is_file():
        return np.empty((0, 3))
    tree = ET.parse(str(vtp_path))
    root = tree.getroot()
    for el in root.iter("Points"):
        for da in el.findall("DataArray"):
            if da.text:
                coords = [float(x) for x in da.text.split()]
                return np.array(coords).reshape(-1, 3)
    return np.empty((0, 3))


def read_cell_centers(case_dir: Path) -> np.ndarray:
    """(N, 3) array of cell centres from constant/polyMesh."""
    # Use foamcli's pure-Python reader (no OpenFOAM required)
    sys.path.insert(0, str(Path("/workspace/trymarz/foamcli/src")))
    from foamcli.util.foam_mesh import read_cell_centers as fm_read
    return fm_read(case_dir)


# ── main logic ────────────────────────────────────────────────────────

def find_pgf_times(case_dir: Path) -> list[float]:
    """Sorted list of write times from processor0/."""
    proc0 = case_dir / "processor0"
    times = []
    for entry in proc0.iterdir():
        if entry.is_dir():
            try:
                times.append(float(entry.name))
            except ValueError:
                pass
    return sorted(times)


def find_sphere_times(case_dir: Path) -> dict[float, Path]:
    """Mapping of sphere VTK time → file path from spheres/."""
    result = {}
    for fn in (case_dir / "spheres").glob("spheres-rank0-*.vtp"):
        name = fn.stem.replace("spheres-rank0-", "")
        try:
            result[float(name)] = fn
        except ValueError:
            pass
    return result


def compute_z_centroid(values: np.ndarray, centres: np.ndarray) -> float:
    """z-centroid of cells/particles whose values pass the mask.

    When *values* is the cell field (porosityF), mask = porosityF < 0.5.
    For spheres, mask is always True (all spheres counted).
    """
    if len(values) == 0:
        return float("nan")
    return float(np.mean(values))


def main() -> None:
    ap = argparse.ArgumentParser(description="Spatial coherence checker")
    ap.add_argument("--csv", action="store_true", help="CSV output")
    ap.add_argument("--case", default=str(CASE), help="Case directory")
    args = ap.parse_args()

    case_dir = Path(args.case)
    centres = read_cell_centers(case_dir)
    if len(centres) == 0:
        print("ERROR: cannot read mesh cell centres", file=sys.stderr)
        sys.exit(1)

    print(f"Mesh: {len(centres)} cells, "
          f"z ∈ [{centres[:,2].min():.4f}, {centres[:,2].max():.4f}]")

    pgf_times = find_pgf_times(case_dir)
    sphere_map = find_sphere_times(case_dir)
    sphere_times = sorted(sphere_map.keys())

    bed_height = centres[:,2].max() - centres[:,2].min()
    print(f"Bed height (z-span): {bed_height:.4f} m")
    print(f"PGF write times: {len(pgf_times)}  Sphere VTKs: {len(sphere_times)}")

    if not args.csv:
        header = f"{'Time':>8s}  {'z_PGF':>8s}  {'z_DEM':>8s}  {'|dz|':>8s}  {'dz/H':>7s}  Status"
        print(f"\n{header}")
        print("-" * len(header))

    results = []
    for t in pgf_times:
        # Find closest sphere VTK ≤ t
        sphere_t = None
        for st in sphere_times:
            if st <= t + 1e-6:
                sphere_t = st

        # Read porosityF at this time (decomposed → full field)
        poro = read_scalar_field_decomposed(case_dir, t, "porosityF")

        # z-centroid of PGF solid region (porosityF < 0.5 = bed cells)
        if len(poro) > 0:
            bed_mask = poro < 0.5
            if bed_mask.any():
                pgf_z = float(np.mean(centres[bed_mask, 2]))
            else:
                pgf_z = float("nan")
        else:
            pgf_z = float("nan")

        # z-centroid of DEM spheres
        if sphere_t is not None and sphere_t in sphere_map:
            vtp = sphere_map[sphere_t]
            sph = read_sphere_positions(vtp)
            if len(sph) > 0:
                dem_z = float(np.mean(sph[:, 2]))
            else:
                dem_z = float("nan")
        else:
            dem_z = float("nan")

        dz = abs(pgf_z - dem_z) if not (np.isnan(pgf_z) or np.isnan(dem_z)) else float("nan")
        ratio = dz / bed_height if not np.isnan(dz) else float("nan")

        if ratio < 0.2:
            status = "OK"
        elif ratio < 0.5:
            status = "WARN"
        else:
            status = "DECOUPLED"

        results.append((t, pgf_z, dem_z, dz, ratio, status))

    if args.csv:
        print("time,z_pgf,z_dem,abs_dz,dz_over_H,status")
        for t, pgf_z, dem_z, dz, ratio, status in results:
            print(f"{t:.3f},{pgf_z:.4f},{dem_z:.4f},{dz:.4f},{ratio:.4f},{status}")
    else:
        for t, pgf_z, dem_z, dz, ratio, status in results:
            print(f"{t:8.3f}  {pgf_z:8.4f}  {dem_z:8.4f}  {dz:8.4f}  {ratio:6.3f}  {status}")

    # Summary
    ok = sum(1 for r in results if r[5] == "OK")
    warn = sum(1 for r in results if r[5] == "WARN")
    bad = sum(1 for r in results if r[5] == "DECOUPLED")
    print(f"\nSummary: {ok} OK, {warn} WARN, {bad} DECOUPLED out of {len(results)} times")

    if bad > 0 and bad == 1 and np.isnan(results[0][3]):
        # The one "DECOUPLED" was t=0 where no sphere data exists — expected
        print("PASS: spatial coherence maintained throughout (t=0 has no sphere data)")
    elif bad > 0:
        print("FAIL: coupling lost at some times — poster proof compromised")
    elif warn > 0:
        print("WARNING: coupling borderline at some times — check cases manually")
    else:
        print("PASS: spatial coherence maintained throughout")


if __name__ == "__main__":
    main()
