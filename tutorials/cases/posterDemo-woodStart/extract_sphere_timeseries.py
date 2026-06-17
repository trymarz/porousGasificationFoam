#!/usr/bin/env python3
"""extract_sphere_timeseries — track selected DEM spheres through time.

Extracts radius r(t) and position z(t) for 3 spheres at different initial
bed heights (top, middle, bottom). Uses nearest-neighbor matching by position
to follow the same sphere across VTK files.

Usage:
    python3 extract_sphere_timeseries.py
    python3 extract_sphere_timeseries.py --csv > sphere_ts.csv
    python3 extract_sphere_timeseries.py --all  # full stats, all times

Output: for each tracked sphere: time, sphere_id, radius, x, y, z.
Also prints overall stats: count, r_mean, r_min, r_max per time step.
"""

from __future__ import annotations

import argparse
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

CASE = Path("/workspace/trymarz/porousGasificationFoam/tutorials/cases/posterDemo-woodStart")


def read_sphere_vtp(vtp_path: Path) -> dict:
    """Return {positions: [(x,y,z),…], radii: [float,…], lambdaDot: [float,…]}."""
    if not vtp_path.is_file():
        return {"positions": [], "radii": [], "lambdaDot": []}
    tree = ET.parse(str(vtp_path))
    root = tree.getroot()
    result = {"positions": [], "radii": [], "lambdaDot": []}
    for el in root.iter("Points"):
        for da in el.findall("DataArray"):
            if da.text:
                coords = [float(x) for x in da.text.split()]
                n = len(coords) // 3
                result["positions"] = [tuple(coords[i * 3:(i + 1) * 3]) for i in range(n)]
    for pd in root.iter("PointData"):
        for da in pd.findall("DataArray"):
            name = da.get("Name") or ""
            if name == "radius" and da.text:
                result["radii"] = [float(x) for x in da.text.split()]
            if name == "lambdaDot" and da.text:
                result["lambdaDot"] = [float(x) for x in da.text.split()]
    return result


def pick_tracked_spheres(vtp_path: Path, n: int = 3) -> list[tuple[float, float, float]]:
    """Pick *n* spheres at different z-heights from the first VTK.

    Returns list of (x, y, z) initial positions for nearest-neighbor matching.
    """
    data = read_sphere_vtp(vtp_path)
    pos = data["positions"]
    if len(pos) < n:
        return pos[:n]

    # Sort by z, pick evenly spaced
    sorted_pos = sorted(pos, key=lambda p: p[2])
    n_avail = len(sorted_pos)
    picks = [0, n_avail // 2, n_avail - 1]
    if n > 3:
        step = n_avail / (n - 1)
        picks = [int(i * step) for i in range(n)]
    return [sorted_pos[i] for i in picks]


def track_spheres(data: dict, track_init: list[tuple]) -> list[tuple]:
    """Match *track_init* positions to closest sphere in *data*.

    Returns list of (z_init, x, y, z, r, lambdaDot, dist) tuples.
    """
    pos = data["positions"]
    radii = data.get("radii", [0.0] * len(pos))
    ld = data.get("lambdaDot", [1.0] * len(pos))

    if not pos:
        return []

    matched = []
    for init_pos in track_init:
        ix, iy, iz = init_pos
        best_i = 0
        best_dist = float("inf")
        for i, (px, py, pz) in enumerate(pos):
            d = math.hypot(px - ix, py - iy, pz - iz)
            if d < best_dist:
                best_dist = d
                best_i = i
        matched.append((iz, pos[best_i][0], pos[best_i][1], pos[best_i][2],
                         radii[best_i], ld[best_i], best_dist))
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
    print(f"Found {len(times)} sphere VTK files, t ∈ [{times[0]:.4f}, {times[-1]:.4f}]", file=sys.stderr)

    # Pick spheres to track from the first VTK
    first_vtp = sphere_map[times[0]]
    track_init = pick_tracked_spheres(first_vtp, n=args.n)
    print(f"Tracking {len(track_init)} spheres:", file=sys.stderr)
    for i, p in enumerate(track_init):
        print(f"  sphere {i}: init_z = {p[2]:.4f} m  ({p[0]:.4f}, {p[1]:.4f}, {p[2]:.4f})", file=sys.stderr)
    print(file=sys.stderr)

    # For --all mode, just print overall stats per time
    if args.all:
        if args.csv:
            print("time,sphere_count,r_mean_mm,r_min_mm,r_max_mm,ld_mean,ld_min,ld_max,z_mean")
        else:
            header = (f"{'Time':>8s}  {'N':>4s}  {'r_mean':>8s}  {'r_min':>8s}  "
                      f"{'r_max':>8s}  {'ld_mean':>8s}  {'z_mean':>8s}")
            print(header)
            print("-" * len(header))
        for t in times:
            vtp = sphere_map[t]
            data = read_sphere_vtp(vtp)
            pos = data["positions"]
            radii = data.get("radii", [])
            ld = data.get("lambdaDot", [])
            n = len(pos)
            if n == 0:
                continue
            r_mm = [r * 1000 for r in radii]
            r_mean = sum(r_mm) / n
            r_min = min(r_mm)
            r_max = max(r_mm)
            ld_mean = sum(ld) / n if ld else 1.0
            ld_min = min(ld) if ld else 1.0
            ld_max = max(ld) if ld else 1.0
            z_mean = sum(p[2] for p in pos) / n
            if args.csv:
                print(f"{t:.3f},{n},{r_mean:.4f},{r_min:.4f},{r_max:.4f},"
                      f"{ld_mean:.4f},{ld_min:.4f},{ld_max:.4f},{z_mean:.4f}")
            else:
                print(f"{t:8.3f}  {n:4d}  {r_mean:8.4f}  {r_min:8.4f}  "
                      f"{r_max:8.4f}  {ld_mean:8.4f}  {z_mean:8.4f}")
        return

    # Track selected spheres through all times
    if args.csv:
        print("time,sphere_id,init_z,x,y,z,radius_mm,lambdaDot,match_dist")
    else:
        header = (f"{'Time':>8s}  {'ID':>4s}  {'z_init':>8s}  {'z_curr':>8s}  "
                  f"{'r(mm)':>8s}  {'lambdaDot':>10s}")
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
