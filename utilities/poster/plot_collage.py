# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "matplotlib"]
# ///
"""Assemble the individual poster figures into one multi-panel collage.

Run extract.py first to produce the PNGs, then:
  uv run utilities/poster/plot_collage.py [CASE_DIR] [-o collage.png]

Lays the panels out in a 2-row grid matching the poster's "SHOW IT" block:
  row 1 (profiles): gas T | solid Ts | gas composition
  row 2 (time series): solid conversion | porosity
Missing panels are skipped, so it works on partial output too.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.image as mpimg
import matplotlib.pyplot as plt

LAYOUT = [
    ["profiles_T.png", "profiles_Ts.png", "profiles_gas.png"],
    ["timeseries_solid.png", "timeseries_porosity.png", None],
]


def build_collage(fig_dir: Path, out: Path, title: str | None) -> bool:
    present = {
        name: fig_dir / name
        for row in LAYOUT
        for name in row
        if name and (fig_dir / name).exists()
    }
    if not present:
        print(f"no panel PNGs found in {fig_dir} — run extract.py first")
        return False

    nrows = len(LAYOUT)
    ncols = max(len(r) for r in LAYOUT)
    fig, axes = plt.subplots(nrows, ncols, figsize=(5 * ncols, 5.5 * nrows))
    axes = axes.reshape(nrows, ncols)

    for r, row in enumerate(LAYOUT):
        for c in range(ncols):
            ax = axes[r, c]
            ax.axis("off")
            name = row[c] if c < len(row) else None
            if name and name in present:
                ax.imshow(mpimg.imread(present[name]))

    if title:
        fig.suptitle(title, fontsize=18, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97 if title else 1))
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}  ({len(present)} panels)")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    repo_default = Path(__file__).resolve().parents[2] / "tutorials/cases/updraftDemo"
    ap.add_argument("case", nargs="?", default=str(repo_default),
                    help="case directory (default: tutorials/cases/updraftDemo)")
    ap.add_argument("-o", "--out", default=None,
                    help="output PNG (default: CASE/poster_figures/collage.png)")
    ap.add_argument("--title", default="Updraft DEM demo — FVM-DEM coupling",
                    help="collage suptitle")
    args = ap.parse_args()

    case = Path(args.case)
    fig_dir = case / "poster_figures"
    out = Path(args.out) if args.out else fig_dir / "collage.png"
    ok = build_collage(fig_dir, out, args.title)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
