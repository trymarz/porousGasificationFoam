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

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


class ComparisonError(ValueError):
    """An output file cannot be compared as a well-formed data file."""


_COMPOUND_RE = re.compile(r"\([^()]*\)")


def _parse_data_row(line: str, line_number: int) -> tuple[float, ...]:
    """Parse scalar tokens and top-level parenthesized OpenFOAM values.

    OpenFOAM writes a vector as one logical value, for example ``(1 2 3)``.
    Flattening it keeps the comparison deterministic while allowing the header
    to retain the logical field name.
    """
    values: list[float] = []
    pos = 0
    while pos < len(line):
        if line[pos].isspace():
            pos += 1
            continue

        if line[pos] == "(":
            match = _COMPOUND_RE.match(line, pos)
            if match is None:
                raise ComparisonError(
                    f"line {line_number}: malformed parenthesized value"
                )
            contents = match.group()[1:-1].split()
            if not contents:
                raise ComparisonError(
                    f"line {line_number}: empty parenthesized value"
                )
            try:
                values.extend(float(token) for token in contents)
            except ValueError as exc:
                raise ComparisonError(
                    f"line {line_number}: non-numeric compound value"
                ) from exc
            pos = match.end()
            continue

        end = pos
        while end < len(line) and not line[end].isspace():
            end += 1
        token = line[pos:end]
        if "(" in token or ")" in token:
            raise ComparisonError(f"line {line_number}: malformed value")
        try:
            values.append(float(token))
        except ValueError as exc:
            raise ComparisonError(
                f"line {line_number}: non-numeric value {token!r}"
            ) from exc
        pos = end

    if not values:
        raise ComparisonError(f"line {line_number}: empty data row")
    return tuple(values)


def parse_dat(path: Path) -> tuple[list[str], list[tuple[float, ...]]]:
    """Return (header_columns, list_of_rows) parsed from a .dat file.

    header_columns: list of column names (Time first), drawn from the LAST
                    comment line before data starts. May be empty if no
                    column header was emitted.
    rows:           list of flattened numeric tuples, one tuple per data line.

    A non-comment, non-empty line is a data row and must be parseable. Silently
    skipping malformed rows would make a partial output look like a pass.
    """
    columns: list[str] = []
    rows: list[tuple[float, ...]] = []
    last_comment: str | None = None
    with open(path, "r") as f:
        for line_number, raw in enumerate(f, start=1):
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                last_comment = line.lstrip("#").strip()
                continue
            row = _parse_data_row(line, line_number)
            if rows and len(row) != len(rows[0]):
                raise ComparisonError(
                    f"line {line_number}: column count differs within file: "
                    f"expected={len(rows[0])} vs found={len(row)}"
                )
            rows.append(row)
    if last_comment:
        columns = last_comment.split()
    return columns, rows


def within_tol(a: float, b: float, rtol: float, atol: float) -> bool:
    return abs(a - b) <= atol + rtol * max(abs(a), abs(b))


def compare_file(
    ref_path: Path, cand_path: Path, rtol: float, atol: float
) -> list[str]:
    """Return numerical failure descriptions; raise for invalid file shapes."""
    failures: list[str] = []
    if not ref_path.is_file():
        raise ComparisonError(f"reference file not found: {ref_path}")
    if not cand_path.is_file():
        raise ComparisonError(f"candidate file not found: {cand_path}")

    try:
        ref_cols, ref_rows = parse_dat(ref_path)
    except ComparisonError as exc:
        raise ComparisonError(f"reference {ref_path}: {exc}") from exc
    try:
        cand_cols, cand_rows = parse_dat(cand_path)
    except ComparisonError as exc:
        raise ComparisonError(f"candidate {cand_path}: {exc}") from exc

    if not ref_rows:
        raise ComparisonError(f"reference {ref_path} contains no data rows")
    if not cand_rows:
        raise ComparisonError(f"candidate {cand_path} contains no data rows")

    if len(ref_rows) != len(cand_rows):
        raise ComparisonError(
            f"row count differs: ref={len(ref_rows)} vs cand={len(cand_rows)}"
        )

    n = len(ref_rows)
    ncols = len(ref_rows[0])
    if len(cand_rows[0]) != ncols:
        raise ComparisonError(
            f"column count differs: ref={ncols} vs cand={len(cand_rows[0])}"
        )

    col_names = (
        ref_cols
        if len(ref_cols) == ncols
        else _expanded_column_names(ref_cols, ncols)
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


def _expanded_column_names(columns: list[str], width: int) -> list[str]:
    """Name flattened components while retaining the logical field name."""
    if len(columns) == 2 and width > 2:
        return [columns[0]] + [f"{columns[1]}[{i}]" for i in range(width - 1)]
    return [f"col{i}" for i in range(width)]


def collect_dat_files(root: Path) -> list[Path]:
    """Return list of paths to *.dat files under root."""
    out: list[Path] = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if fn.endswith(".dat"):
                out.append(Path(dirpath) / fn)
    return out


def main() -> int:
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
    invalid = 0

    for ref_path in sorted(ref_files):
        rel = ref_path.relative_to(ref_root)
        cand_path = cand_root / rel
        if not cand_path.is_file():
            print(f"ERROR {rel} (no candidate file)", file=sys.stderr)
            missing += 1
            continue
        try:
            failures = compare_file(ref_path, cand_path, args.rtol, args.atol)
        except ComparisonError as exc:
            print(f"ERROR {rel}: {exc}", file=sys.stderr)
            invalid += 1
            continue
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
        f"compareScalars.py: {total - failed - missing - invalid}/{total} files within tolerance "
        f"(rtol={args.rtol}, atol={args.atol})"
    )
    if missing:
        print(
            f"compareScalars.py: {missing} reference file(s) not present in candidate"
        )
    if invalid:
        print(f"compareScalars.py: {invalid} file(s) had invalid data", file=sys.stderr)

    if missing > 0 or invalid > 0:
        return 2
    if failed > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
