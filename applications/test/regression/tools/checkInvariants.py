#!/usr/bin/env python3
"""Check a case's declared invariants against its postProcessing output.

The baseline comparison (compareScalars.py) says a column reproduced itself.
An invariant says what the column must be whatever the baseline holds: a
conserved total, an exact zero, a bound, a prescribed value. A run that breaks
one has a bug; captureBaseline.sh refuses to capture it.

Invariant file (a case's system/regressionInvariants), one per line, `#` comments:

    <functionObject> <column> zero      ATOL        every |x| <= ATOL
    <functionObject> <column> constant  RTOL        max - min <= RTOL * max|x|
    <functionObject> <column> <=        BOUND [TOL] every x <= BOUND + TOL
    <functionObject> <column> >=        BOUND [TOL] every x >= BOUND - TOL
    <functionObject> <column> ==        VALUE TOL   every |x - VALUE| <= TOL

<column> is the name in the .dat header, e.g. volIntegrate(Ycharm). Every
.dat under postProcessing/<functionObject>/ (all start times) is checked.

Exit codes:
    0  every invariant holds.
    1  at least one invariant is broken (each failure printed).
    2  infrastructure error: unreadable file, unknown check, missing column.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compareScalars import ComparisonError, collect_dat_files, parse_dat  # noqa: E402

CHECKS = {"zero": (1, 1), "constant": (1, 1), "<=": (1, 2), ">=": (1, 2), "==": (2, 2)}


class InvariantError(ValueError):
    """The invariant file or the output it refers to cannot be checked."""


def parse_invariants(path: Path) -> list[tuple[int, str, str, str, list[float]]]:
    """Return (line, functionObject, column, check, args) for each declared invariant."""
    out = []
    for n, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        tokens = line.split()
        if len(tokens) < 3 or tokens[2] not in CHECKS:
            raise InvariantError(f"{path}:{n}: expected '<functionObject> <column> <check> ...'"
                                 f" with check one of {', '.join(CHECKS)}")
        fo, column, check, args = tokens[0], tokens[1], tokens[2], tokens[3:]
        lo, hi = CHECKS[check]
        if not lo <= len(args) <= hi:
            raise InvariantError(f"{path}:{n}: '{check}' takes {lo}..{hi} numbers, got {len(args)}")
        try:
            values = [float(a) for a in args]
        except ValueError as exc:
            raise InvariantError(f"{path}:{n}: {exc}") from exc
        out.append((n, fo, column, check, values))
    if not out:
        raise InvariantError(f"{path}: declares no invariants")
    return out


def column_values(post: Path, fo: str, column: str) -> list[float]:
    """Every value of `column` in every .dat under post/fo, in file then row order."""
    files = sorted(collect_dat_files(post / fo))
    if not files:
        raise InvariantError(f"no .dat under {post / fo}")
    values: list[float] = []
    found = False
    for f in files:
        try:
            columns, rows = parse_dat(f)
        except ComparisonError as exc:
            raise InvariantError(f"{f}: {exc}") from exc
        if column not in columns:
            continue
        if rows and len(columns) != len(rows[0]):
            raise InvariantError(f"{f}: {column} is not a scalar column")
        found = True
        j = columns.index(column)
        values.extend(r[j] for r in rows)
    if not found:
        raise InvariantError(f"no column {column} in {post / fo}")
    if not values:
        raise InvariantError(f"{column} in {post / fo} has no data rows")
    return values


def violations(values: list[float], check: str, args: list[float]) -> list[str]:
    """Describe each way `values` break the check; empty when it holds."""
    if check == "constant":
        lo, hi = min(values), max(values)
        limit = args[0] * max(abs(lo), abs(hi))
        return [] if hi - lo <= limit else [f"spans {lo:.10e} .. {hi:.10e}, {hi - lo:.3e} > {limit:.3e}"]
    if check == "zero":
        bad = [(i, x) for i, x in enumerate(values) if not abs(x) <= args[0]]
    elif check == "<=":
        bad = [(i, x) for i, x in enumerate(values) if not x <= args[0] + (args[1] if len(args) > 1 else 0.0)]
    elif check == ">=":
        bad = [(i, x) for i, x in enumerate(values) if not x >= args[0] - (args[1] if len(args) > 1 else 0.0)]
    else:
        bad = [(i, x) for i, x in enumerate(values) if not abs(x - args[0]) <= args[1]]
    return [f"row {i}: {x:.10e}" for i, x in bad]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--invariants", required=True, type=Path, help="the case's system/regressionInvariants")
    ap.add_argument("--postprocessing", required=True, type=Path, help="the run's postProcessing/ directory")
    a = ap.parse_args()

    failed = 0
    try:
        invariants = parse_invariants(a.invariants)
        for n, fo, column, check, args in invariants:
            bad = violations(column_values(a.postprocessing, fo, column), check, args)
            spec = f"{fo} {column} {check} {' '.join(f'{x:g}' for x in args)}"
            print(f"{'FAIL' if bad else 'OK  '}  {spec}")
            for b in bad[:5]:
                print(f"        {b}")
            if len(bad) > 5:
                print(f"        ... and {len(bad) - 5} more")
            failed += bool(bad)
    except (InvariantError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print(f"checkInvariants.py: {len(invariants) - failed}/{len(invariants)} invariants hold")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
