# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "matplotlib"]
# ///
"""Vertical-profile plots for the updraftDemo poster.

Parses an OpenFOAM `probes` function-object output file (the
`centerlineProbes` set written by updraftDemo) and plots a chosen field as a
function of column height z, with one curve per time snapshot.

Usable two ways:
  * imported  -> use parse_probe_file() / plot_profile()
  * standalone-> `uv run plot_profiles.py <case>/postProcessing/centerlineProbes/0/T`
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

_PROBE_RE = re.compile(r"#\s*Probe\s+(\d+)\s+\(([^)]+)\)")


def parse_probe_file(path: str | Path):
    """Read an OpenFOAM probes file.

    Returns (z, times, data) where
      z     : (nProbe,)        probe heights (z-coordinate)
      times : (nTime,)         output times
      data  : (nTime, nProbe)  field value at each probe and time
    """
    path = Path(path)
    z_coords: dict[int, float] = {}
    times: list[float] = []
    rows: list[list[float]] = []

    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            m = _PROBE_RE.match(line)
            if m:
                idx = int(m.group(1))
                # probe coordinate is "x y z" -> take the third (height)
                z_coords[idx] = float(m.group(2).split()[2])
            continue
        parts = line.split()
        times.append(float(parts[0]))
        rows.append([float(v) for v in parts[1:]])

    z = np.array([z_coords[i] for i in sorted(z_coords)])
    return z, np.array(times), np.array(rows)


def _pick_snapshots(times: np.ndarray, n: int = 4) -> list[int]:
    """Indices of ~n evenly spaced snapshots (always includes the last)."""
    if len(times) <= n:
        return list(range(len(times)))
    return list(np.linspace(0, len(times) - 1, n).round().astype(int))


def plot_profile(path, label=None, ax=None, n_snapshots=4):
    """Plot field-vs-height for several time snapshots. Returns the Axes."""
    z, times, data = parse_probe_file(path)
    if ax is None:
        _, ax = plt.subplots(figsize=(4, 5))
    label = label or Path(path).name
    for i in _pick_snapshots(times, n_snapshots):
        ax.plot(data[i], z, marker="o", ms=3, label=f"t = {times[i]:g} s")
    ax.set_xlabel(label)
    ax.set_ylabel("height z [m]")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=7)
    return ax


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    src = argv[1]
    out = argv[2] if len(argv) > 2 else f"profile_{Path(src).name}.png"
    ax = plot_profile(src, label=Path(src).name)
    ax.figure.tight_layout()
    ax.figure.savefig(out, dpi=150)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
