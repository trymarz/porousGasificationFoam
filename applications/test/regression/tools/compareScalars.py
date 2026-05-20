#!/usr/bin/env python3
"""Compare OpenFOAM postProcessing/volFieldValue output against a reference baseline.

Walks `--reference` recursively, finds every `.dat` file produced by
function objects, locates the matching file under `--candidate`, and compares
row-by-row, column-by-column with relative + absolute tolerance.

File format expected (verified against gitlab.com/openfoam/core/openfoam,
src/functionObjects/field/fieldValues/volFieldValue/volFieldValue.C
writeFileHeader):
    - Comment lines start with `#`.
    - Header line names columns; first column is `Time`, subsequent columns
      have names like `volIntegrate(porosity)`.
    - Data rows are whitespace/tab-separated numeric values.

Exit codes:
    0  PASS, every numeric value within tolerance.
    1  FAIL, at least one value out of tolerance (per-row diff printed).
    2  Infrastructure error (reference file missing in candidate, etc.).
"""

import argparse
import os
import sys
from pathlib import Path


def parse_dat(path):
    """Return (header_columns, list_of_rows) parsed from a .dat file.

    header_columns: list of column names (Time first), drawn from the LAST
                    comment line before data starts. May be empty if no
                    column header was emitted.
    rows:           list of tuples of floats, one tuple per data line.
    """
    columns = []
    rows = []
    last_comment = None
    with open(path, "r") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                last_comment = line.lstrip("#").strip()
                continue
            tokens = line.split()
            try:
                rows.append(tuple(float(t) for t in tokens))
            except ValueError:
                # Skip non-numeric line (defensive).
                continue
    if last_comment:
        columns = last_comment.split()
    return columns, rows


def within_tol(a, b, rtol, atol):
    return abs(a - b) <= atol + rtol * max(abs(a), abs(b))


def compare_file(ref_path, cand_path, rtol, atol):
    """Return list of failure descriptions; empty list means PASS."""
    failures = []
    ref_cols, ref_rows = parse_dat(ref_path)
    cand_cols, cand_rows = parse_dat(cand_path)

    if len(ref_rows) != len(cand_rows):
        failures.append(
            f"row count differs: ref={len(ref_rows)} vs cand={len(cand_rows)}"
        )
        # Continue with min length so we still report numeric divergence.

    n = min(len(ref_rows), len(cand_rows))
    if n == 0:
        return failures

    ncols = len(ref_rows[0])
    for cand_row in cand_rows[:n]:
        if len(cand_row) != ncols:
            failures.append(
                f"column count differs: ref={ncols} vs cand={len(cand_row)}"
            )
            return failures

    col_names = (
        ref_cols if len(ref_cols) == ncols else [f"col{i}" for i in range(ncols)]
    )

    for i in range(n):
        for j in range(ncols):
            a = ref_rows[i][j]
            b = cand_rows[i][j]
            if not within_tol(a, b, rtol, atol):
                failures.append(
                    f"row {i} col {j} ({col_names[j]}): "
                    f"ref={a:.8e} cand={b:.8e} "
                    f"abs_diff={abs(a - b):.3e} "
                    f"rel_diff={abs(a - b) / max(abs(a), abs(b), 1e-300):.3e}"
                )
    return failures


def collect_dat_files(root):
    """Return list of paths to *.dat files under root."""
    out = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if fn.endswith(".dat"):
                out.append(Path(dirpath) / fn)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--reference",
        required=True,
        help="reference postProcessing/ directory (the baseline)",
    )
    ap.add_argument(
        "--candidate",
        required=True,
        help="candidate postProcessing/ directory (fresh run)",
    )
    ap.add_argument(
        "--rtol", type=float, default=1e-4, help="relative tolerance (default: 1e-4)"
    )
    ap.add_argument(
        "--atol", type=float, default=1e-12, help="absolute tolerance (default: 1e-12)"
    )
    args = ap.parse_args()

    ref_root = Path(args.reference).resolve()
    cand_root = Path(args.candidate).resolve()

    if not ref_root.is_dir():
        print(f"ERROR: reference dir not found: {ref_root}", file=sys.stderr)
        return 2
    if not cand_root.is_dir():
        print(f"ERROR: candidate dir not found: {cand_root}", file=sys.stderr)
        return 2

    ref_files = collect_dat_files(ref_root)
    if not ref_files:
        print(f"ERROR: no .dat files found under {ref_root}", file=sys.stderr)
        return 2

    total = len(ref_files)
    failed = 0
    missing = 0

    for ref_path in sorted(ref_files):
        rel = ref_path.relative_to(ref_root)
        cand_path = cand_root / rel
        if not cand_path.is_file():
            print(f"MISS  {rel} (no candidate file)", file=sys.stderr)
            missing += 1
            continue
        failures = compare_file(ref_path, cand_path, args.rtol, args.atol)
        if failures:
            failed += 1
            print(f"FAIL  {rel}")
            # Cap the per-file failure output so massive divergences don't
            # flood the terminal; show first 10 rows of disagreement.
            for f in failures[:10]:
                print(f"        {f}")
            if len(failures) > 10:
                print(f"        ... and {len(failures) - 10} more")
        else:
            print(f"OK    {rel}")

    print()
    print(
        f"compareScalars.py: {total - failed - missing}/{total} files within tolerance "
        f"(rtol={args.rtol}, atol={args.atol})"
    )
    if missing:
        print(
            f"compareScalars.py: {missing} reference file(s) not present in candidate"
        )

    if failed > 0 or missing > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
