#!/usr/bin/env python3
"""extract_sphere_timeseries — track selected DEM spheres through time.

Extracts radius r(t) and position z(t) for 3 spheres at different initial
bed heights (top, middle, bottom). Uses nearest-neighbor matching by z-position
to follow the same sphere across VTK files.

Usage:
    uv run python extract_sphere_timeseries.py
    uv run python extract_sphere_timeseries.py --csv > sphere_ts.csv
    uv run python extract_sphere_timeseries.py --all  # full stats, all times

Output: for each tracked sphere: time, sphere_id, radius, x, y, z.
Also prints overall stats: count, r_mean, r_min, r_max per time step.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

CASE = Path("/workspace/trymarz/porousGasificationFoam/tutorials/cases/posterDemo-layers")


def read_sphere_vtp(vtp_path: Path) -> dict[str, np.ndarray]:
    """Return {positions: (N,3), radii: (N,), lambdaDot: (N,)}."""
    if not vtp_path.is_file():
        return {"positions": np.empty((0, 3)), "radii": np.empty(0), "lambdaDot": np.empty(0)}
    tree = ET.parse(str(vtp_path))
    root = tree.getroot()
    result: dict[str, np.ndarray] = {}
    for el in root.iter("Points"):
        for da in el.findall("DataArray"):
            if da.text:
                coords = [float(x) for x in da.text.split()]
                result["positions"] = np.array(coords).reshape(-1, 3)
    for pd in root.iter("PointData"):
        for da in pd.findall("DataArray"):
            if da.get("Name") == "radius" and da.text:
                result["radii"] = np.array([float(x) for x in da.text.split()])
            if da.get("Name") == "lambdaDot" and da.text:
                result["lambdaDot"] = np.array([float(x) for x in da.text.split()])
    return result


def pick_tracked_spheres(vtp_path: Path, n: int = 3) -> np.ndarray:
    """Pick *n* spheres at different z-heights from the first VTK.

    Returns (n, 3) array of initial positions used for nearest-neighbor matching.
    """
    data = read_sphere_vtp(vtp_path)
    pos = data["positions"]
    if len(pos) < n:
        return pos[:, :3]

    # Sort by z, pick evenly spaced
    idx = np.argsort(pos[:, 2])
    n_avail = len(pos)

    # Pick at indices: 0 (bottom), n_avail//2 (mid), n_avail-1 (top)
    picks = [0, n_avail // 2, n_avail - 1]
    if n > 3:
        # Add evenly spaced intermediates
        step = n_avail / (n - 1)
        picks = [int(i * step) for i in range(n)]

    init_positions = pos[idx[picks]]
    return init_positions[:, :3]


def track_spheres(
    data: dict[str, np.ndarray], track_init: np.ndarray
) -> np.ndarray:
    """Match *track_init* positions to closest sphere in *data*.

    Returns (n_tracked, 7) array: [z_init, x, y, z, r, lambdaDot, dist]
    where dist is the distance from the tracked position to the match.
    """
    pos = data["positions"]
    radii = data.get("radii", np.zeros(len(pos)))
    ld = data.get("lambdaDot", np.ones(len(pos)))

    if len(pos) == 0:
        return np.empty((len(track_init), 7))

    matched = np.zeros((len(track_init), 7))
    for i, init_pos in enumerate(track_init):
        # Find nearest neighbor by 3D distance
        dists = np.linalg.norm(pos - init_pos, axis=1)
        closest = np.argmin(dists)
        matched[i] = [
            init_pos[2],  # z_init for tracking label
            pos[closest, 0],
            pos[closest, 1],
            pos[closest, 2],
            radii[closest],
            ld[closest],
            dists[closest],
        ]
    return matched


def find_sphere_times(case_dir: Path) -> dict[float, Path]:
    """Mapping of sphere VTK time → file path from spheres/."""
    result = {}
    for fn in sorted((case_dir / "spheres").glob("spheres-rank0-*.vtp")):
        name = fn.stem.replace("spheres-rank0-", "")
        try:
            result[float(name)] = fn
        except ValueError:
            pass
    return result


def main() -> None:
    ap = argparse.ArgumentParser(description="Sphere radius time series")
    ap.add_argument("--csv", action="store_true", help="CSV output")
    ap.add_argument("--all", action="store_true", help="Full stats for all times")
    ap.add_argument("--case", default=str(CASE), help="Case directory")
    ap.add_argument("-n", type=int, default=3, help="Number of spheres to track")
    args = ap.parse_args()

    case_dir = Path(args.case)
    sphere_map = find_sphere_times(case_dir)
    if not sphere_map:
        print("No sphere VTK files found", file=sys.stderr)
        sys.exit(1)

    times = sorted(sphere_map.keys())
    print(f"Found {len(times)} sphere VTK files, t ∈ [{times[0]:.4f}, {times[-1]:.4f}]")

    # Pick spheres to track from the first VTK
    first_vtp = sphere_map[times[0]]

    track_init = pick_tracked_spheres(first_vtp, n=args.n)
    print(f"Tracking {len(track_init)} spheres:")
    for i, p in enumerate(track_init):
        print(f"  sphere {i}: init_z = {p[2]:.4f} m  ({p[0]:.4f}, {p[1]:.4f}, {p[2]:.4f})")
    print()

    # For --all mode, just print overall stats per time
    if args.all:
        if args.csv:
            print("time,sphere_count,r_mean_mm,r_min_mm,r_max_mm,ld_mean,ld_min,ld_max,z_mean")
        else:
            header = f"{'Time':>8s}  {'N':>4s}  {'r_mean':>8s}  {'r_min':>8s}  {'r_max':>8s}  {'ld_mean':>8s}  {'z_mean':>8s}"
            print(header)
            print("-" * len(header))
        for t in times:
            vtp = sphere_map[t]
            data = read_sphere_vtp(vtp)
            pos = data["positions"]
            radii = data.get("radii", np.empty(0))
            ld = data.get("lambdaDot", np.ones(len(pos)))
            n = len(pos)
            if n == 0:
                continue
            r_mm = radii * 1000  # convert to mm
            if args.csv:
                print(f"{t:.3f},{n},{r_mm.mean():.4f},{r_mm.min():.4f},{r_mm.max():.4f},"
                      f"{ld.mean():.4f},{ld.min():.4f},{ld.max():.4f},{pos[:,2].mean():.4f}")
            else:
                print(f"{t:8.3f}  {n:4d}  {r_mm.mean():8.4f}  {r_mm.min():8.4f}  "
                      f"{r_mm.max():8.4f}  {ld.mean():8.4f}  {pos[:,2].mean():8.4f}")
        return

    # Track selected spheres through all times
    if args.csv:
        print("time,sphere_id,init_z,x,y,z,radius_mm,lambdaDot,match_dist")
    else:
        header = f"{'Time':>8s}  {'ID':>4s}  {'z_init':>8s}  {'z_curr':>8s}  {'r(mm)':>8s}  {'lambdaDot':>10s}"
        print(header)
        print("-" * len(header))

    for t in times:
        vtp = sphere_map[t]
        data = read_sphere_vtp(vtp)
        matched = track_spheres(data, track_init)

        for i, m in enumerate(matched):
            z_init, x, y, z, r, ld, dist = m
            r_mm = r * 1000
            if args.csv:
                print(f"{t:.3f},{i},{z_init:.4f},{x:.4f},{y:.4f},{z:.4f},{r_mm:.4f},{ld:.6f},{dist:.6f}")
            else:
                print(f"{t:8.3f}  {i:4d}  {z_init:8.4f}  {z:8.4f}  {r_mm:8.4f}  {ld:10.6f}")


if __name__ == "__main__":
    main()
