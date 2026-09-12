#!/usr/bin/env python3
"""poster-viz — generate sphere shrinkage and CFD snapshots for poster preview.

Requires: matplotlib, + xml.etree (stdlib), pathlib (stdlib)
Output: PNG images in ./poster-viz/
"""

import xml.etree.ElementTree as ET
import re
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

CASE = Path("/workspace/trymarz/porousGasificationFoam/tutorials/cases/posterDemo-woodStart")
OUT = CASE / "poster-viz"
OUT.mkdir(parents=True, exist_ok=True)

R0 = 0.003  # initial radius, m
R0_MM = R0 * 1000


def read_vtp(path: Path) -> tuple[list, list]:
    """Return (positions, radii) from a VTP file."""
    if not path.is_file():
        return [], []
    tree = ET.parse(str(path))
    root = tree.getroot()
    positions, radii = [], []
    for el in root.iter("Points"):
        for da in el.findall("DataArray"):
            if da.text:
                coords = [float(x) for x in da.text.split()]
                positions = [(coords[i*3], coords[i*3+1], coords[i*3+2]) for i in range(len(coords)//3)]
    for pd in root.iter("PointData"):
        for da in pd.findall("DataArray"):
            name = da.get("Name") or ""
            if name == "radius" and da.text:
                radii = [float(x) for x in da.text.split()]
    return positions, radii


def read_of_scalar(path: Path) -> tuple[list, int]:
    """Return (values, n_cells) from an OpenFOAM volScalarField."""
    if not path.is_file():
        return [], 0
    data = path.read_bytes()
    idx = data.find(b"internalField")
    if idx < 0:
        return [], 0
    tail = data[idx:].decode("ascii", errors="ignore")
    m = re.search(r"\(", tail)
    if not m:
        m2 = re.search(r"uniform\s+([\d.e+\-]+)", tail)
        if m2:
            # Count cells from polyMesh
            ncells = 48
            val = float(m2.group(1))
            return [val] * ncells, ncells
        return [], 0
    start = m.start()
    depth = 0
    end = start
    for i in range(start, len(tail)):
        if tail[i] == "(": depth += 1
        elif tail[i] == ")":
            depth -= 1
            if depth == 0:
                end = i
                break
    block = tail[start:end + 1]
    vals = [float(x) for x in re.findall(r"[\d.e+\-]+", block)]
    return vals, len(vals)


def find_vtp_times() -> dict[float, Path]:
    """Mapping of time → VTP path."""
    result = {}
    for fn in sorted((CASE / "spheres").glob("spheres-rank0-*.vtp")):
        t = float(fn.stem.replace("spheres-rank0-", ""))
        result[t] = fn
    return result


def find_of_times() -> list[float]:
    """Sorted list of write times from processor0/."""
    proc0 = CASE / "processor0"
    times = []
    for entry in proc0.iterdir():
        if entry.is_dir():
            try:
                times.append(float(entry.name))
            except ValueError:
                pass
    return sorted(times)


def get_closest_vtp(target_t: float, vtp_map: dict[float, Path]) -> Path:
    """Select VTK file nearest to target time."""
    best = None
    best_d = float("inf")
    for t, p in vtp_map.items():
        d = abs(t - target_t)
        if d < best_d:
            best_d = d
            best = p
    return best


# ── Sphere bed side-view ─────────────────────────────────────────────

def draw_sphere_bed():
    """Three-panel figure: spheres in x-z projection at early/mid/late."""
    vtp_map = find_vtp_times()
    if not vtp_map:
        print("No VTK files found")
        return

    times = sorted(vtp_map.keys())
    t_early = times[0]
    t_mid = times[len(times) // 2]
    t_late = times[-1]

    fig, axes = plt.subplots(1, 3, figsize=(18, 5), sharey=True)
    fig.suptitle("Sphere bed — x‑z projection, coloured by radius (mm)", fontsize=14, fontweight="bold")

    for ax, t, label in zip(axes,
                            [t_early, t_mid, t_late],
                            [f"t = {t_early:.2f} s (start)",
                             f"t = {t_mid:.2f} s (mid)",
                             f"t = {t_late:.2f} s (latest)"]):
        pos, radii = read_vtp(vtp_map[t])
        if not pos:
            continue
        r_mm = [r * 1000 for r in radii]
        xs = [p[0] for p in pos]
        zs = [p[2] for p in pos]
        sc = ax.scatter(xs, zs, c=r_mm, s=15, cmap="RdYlBu_r",
                        vmin=0.75, vmax=R0_MM, edgecolors="face", alpha=0.8)
        ax.set_title(label, fontsize=11)
        ax.set_xlabel("x (m)")
        if ax == axes[0]:
            ax.set_ylabel("z (m)")
        ax.set_xlim(0, 0.04)
        ax.set_ylim(0, 0.096)
        ax.set_aspect("equal", "box")

    cbar = fig.colorbar(sc, ax=axes, orientation="vertical", shrink=0.6, pad=0.02)
    cbar.set_label("radius (mm)")

    out = OUT / "spheres_3panel.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  → {out.name}")


# ── CFD vertical profiles ────────────────────────────────────────────

def draw_cfd_profiles():
    """Vertical profiles of Ychar, Ywood, T, porosityF at latest time."""
    of_times = find_of_times()
    if not of_times:
        print("No OF time dirs")
        return

    t = of_times[-1]
    tdir = CASE / "processor0" / f"{t:g}"

    # Cell centres (rough: 12 z‑cells, dz = 0.096/12 = 0.008 m)
    dz = 0.008
    z_centres = [0.004 + i * dz for i in range(12)]

    fields = {
        "Ywood": ("Ywood", "Ywood", "RdYlGn"),
        "Ychar": ("Ychar", "Ychar", "OrRd"),
        "T": ("T", "T (K)", "plasma"),
        "porosityF": ("porosityF", "porosityF", "Blues"),
    }

    fig, axes = plt.subplots(1, 4, figsize=(18, 5), sharey=True)
    fig.suptitle(f"CFD fields — vertical profiles at t = {t:.3f} s", fontsize=14, fontweight="bold")

    for ax, (fname, flabel, cmap) in zip(axes, fields.values()):
        vals, ncells = read_of_scalar(tdir / fname)
        if ncells == 0:
            ax.text(0.5, 0.5, "no data", ha="center", va="center")
            continue
        # 4 cells per z-layer, average per layer
        nz = ncells // 4
        layer_means = [sum(vals[i*4:(i+1)*4]) / 4 for i in range(nz)]
        z_here = [0.004 + i * dz for i in range(nz)]
        ax.plot(layer_means, z_here, "o-", markersize=6, linewidth=2)
        ax.set_xlabel(flabel)
        if ax == axes[0]:
            ax.set_ylabel("z (m)")
        ax.grid(True, alpha=0.3)
        # try colour map on points
        sc = ax.scatter(layer_means, z_here, c=layer_means, cmap=cmap, s=30, zorder=5)
        sc = ax.scatter(layer_means, z_here, c=layer_means, cmap=cmap, s=30, zorder=5)

    out = OUT / "cfd_profiles.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  → {out.name}")


# ── Time-series plot ─────────────────────────────────────────────────

def draw_timeseries():
    """r(t), Ychar(t), porosityF(t) from write times."""
    vtp_map = find_vtp_times()
    of_times = find_of_times()

    # Sphere radii over time
    vtp_times = sorted(vtp_map.keys())
    r_means = []
    for t in vtp_times:
        _, radii = read_vtp(vtp_map[t])
        if radii:
            r_means.append((t, sum(radii) / len(radii) * 1000))

    # Ychar over time
    ychar_vals = []
    poro_vals = []
    for t in of_times:
        tdir = CASE / "processor0" / f"{t:g}"
        yv, _ = read_of_scalar(tdir / "Ychar")
        pv, _ = read_of_scalar(tdir / "porosityF")
        if yv:
            ychar_vals.append((t, sum(yv) / len(yv)))
        if pv:
            poro_vals.append((t, sum(pv) / len(pv)))

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    fig.suptitle("Time evolution", fontsize=14, fontweight="bold")

    # Shrinkage
    ax = axes[0]
    ts, rs = zip(*r_means) if r_means else ([], [])
    ax.plot(ts, rs, "o-", markersize=3, color="steelblue")
    ax.axhline(y=0.75, color="red", linestyle="--", alpha=0.5, label="CHAR_CORE (0.75 mm)")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("mean radius (mm)")
    ax.set_title("Sphere shrinkage")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Ychar
    ax = axes[1]
    if ychar_vals:
        ts, ys = zip(*ychar_vals)
        ax.plot(ts, ys, "o-", markersize=3, color="darkorange")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Ychar")
    ax.set_title("Char mass fraction")
    ax.grid(True, alpha=0.3)

    # Porosity
    ax = axes[2]
    if poro_vals:
        ts, ps = zip(*poro_vals)
        ax.plot(ts, ps, "o-", markersize=3, color="seagreen")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("porosityF")
    ax.set_title("Domain-averaged porosity")
    ax.grid(True, alpha=0.3)

    out = OUT / "timeseries.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  → {out.name}")


# ── Main ─────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print(f"Generating poster previews in {OUT}/ …")
    draw_sphere_bed()
    draw_cfd_profiles()
    draw_timeseries()
    print(f"Done — {len(list(OUT.glob('*.png')))} images in {OUT}/")
