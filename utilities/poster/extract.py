# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "matplotlib"]
# ///
"""Extract poster figures from an updraftDemo run.

Reads the `postProcessing/` output of the updraftDemo case (centerlineProbes,
solidMass, porosityAve) and writes a set of PNG figures used by the GRC 2026
poster:

  profiles_T.png        vertical gas-temperature profiles at time snapshots
  profiles_Ts.png       vertical solid-temperature profiles
  profiles_gas.png      gas composition vs height at the final time
  timeseries_solid.png  wood decay / char growth (volume integrals) vs time
  timeseries_porosity.png  domain-averaged porosity vs time

Usage:
  uv run utilities/poster/extract.py [CASE_DIR] [-o OUT_DIR]

CASE_DIR defaults to tutorials/cases/updraftDemo (relative to repo root).
The mid-plane VTK slices (postProcessing/samplePlane/) and the YADE particle
animation (spheres/spheres.pvd) are best rendered in ParaView and are not
produced here.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from plot_profiles import parse_probe_file, plot_profile, _pick_snapshots

GAS_SPECIES = ["CO", "CO2", "H2", "H2O", "CH4", "tar"]


def parse_vol_field_value(path: str | Path):
    """Parse an OpenFOAM volFieldValue.dat. Returns (times, {field: values})."""
    path = Path(path)
    fields: list[str] = []
    times: list[float] = []
    cols: list[list[float]] = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            # the last comment line before data names the columns
            if "Time" in line:
                # e.g. "# Time   volIntegrate(Ywood)  volIntegrate(Ychar)"
                fields = line.lstrip("#").split()[1:]
            continue
        parts = line.split()
        times.append(float(parts[0]))
        cols.append([float(v) for v in parts[1:]])
    arr = np.array(cols) if cols else np.empty((0, len(fields)))
    return np.array(times), {f: arr[:, i] for i, f in enumerate(fields)}


def fig_profile(probe_dir: Path, field: str, out: Path, xlabel: str):
    src = probe_dir / field
    if not src.exists():
        print(f"  skip {field}: {src} missing")
        return
    ax = plot_profile(src, label=xlabel)
    ax.set_title(f"{field} vs height")
    ax.figure.tight_layout()
    ax.figure.savefig(out, dpi=150)
    plt.close(ax.figure)
    print(f"  wrote {out.name}")


def fig_gas_composition(probe_dir: Path, out: Path):
    """Gas mass fractions vs height at the final snapshot."""
    fig, ax = plt.subplots(figsize=(4.5, 5))
    plotted = False
    for sp in GAS_SPECIES:
        src = probe_dir / sp
        if not src.exists():
            continue
        z, times, data = parse_probe_file(src)
        ax.plot(data[-1], z, marker="o", ms=3, label=sp)
        plotted = True
    if not plotted:
        plt.close(fig)
        print("  skip gas composition: no species probe files")
        return
    ax.set_xlabel("mass fraction [-]")
    ax.set_ylabel("height z [m]")
    ax.set_title("Gas composition (final time)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out.name}")


def fig_solid_mass(case: Path, out: Path):
    src = _latest(case / "postProcessing" / "solidMass")
    if src is None:
        print("  skip solid mass: no solidMass output")
        return
    times, cols = parse_vol_field_value(src)
    fig, ax = plt.subplots(figsize=(5, 4))
    for name, vals in cols.items():
        ax.plot(times, vals, marker=".", label=name)
    ax.set_xlabel("time [s]")
    ax.set_ylabel("volume integral [m^3]")
    ax.set_title("Solid conversion: wood -> char")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out.name}")


def fig_porosity(case: Path, out: Path):
    src = _latest(case / "postProcessing" / "porosityAve")
    if src is None:
        print("  skip porosity: no porosityAve output")
        return
    times, cols = parse_vol_field_value(src)
    fig, ax = plt.subplots(figsize=(5, 4))
    for name, vals in cols.items():
        ax.plot(times, vals, marker=".", color="tab:green", label=name)
    ax.set_xlabel("time [s]")
    ax.set_ylabel("domain-average porosity [-]")
    ax.set_title("Bed opening up as solid gasifies")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out.name}")


def _latest(fo_dir: Path):
    """Return the volFieldValue.dat from the latest time subdir, or None."""
    if not fo_dir.is_dir():
        return None
    candidates = sorted(
        fo_dir.glob("*/volFieldValue.dat"),
        key=lambda p: float(p.parent.name),
    )
    return candidates[-1] if candidates else None


def _latest_probe_dir(case: Path) -> Path | None:
    base = case / "postProcessing" / "centerlineProbes"
    if not base.is_dir():
        return None
    subs = sorted(base.glob("*/"), key=lambda p: float(p.name))
    return subs[-1] if subs else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    repo_default = Path(__file__).resolve().parents[2] / "tutorials/cases/updraftDemo"
    ap.add_argument("case", nargs="?", default=str(repo_default),
                    help="case directory (default: tutorials/cases/updraftDemo)")
    ap.add_argument("-o", "--out", default=None,
                    help="output directory (default: CASE/poster_figures)")
    args = ap.parse_args()

    case = Path(args.case)
    out = Path(args.out) if args.out else case / "poster_figures"
    out.mkdir(parents=True, exist_ok=True)
    print(f"case: {case}\nout:  {out}")

    probe_dir = _latest_probe_dir(case)
    if probe_dir is not None:
        fig_profile(probe_dir, "T", out / "profiles_T.png", "gas T [K]")
        fig_profile(probe_dir, "Ts", out / "profiles_Ts.png", "solid Ts [K]")
        fig_gas_composition(probe_dir, out / "profiles_gas.png")
    else:
        print("  no centerlineProbes output found")

    fig_solid_mass(case, out / "timeseries_solid.png")
    fig_porosity(case, out / "timeseries_porosity.png")
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
