#!/usr/bin/env python3
"""Assert dimensions and sign of OpenFOAM volScalarFields, restricted to
cells selected by a mask field, for a single decomposed case.

Built for the lambdaDot regression fixtures (tutorials/cases/MicroTGA-DEM_*):
reads the fields written at the case's last time step, across all
processor* directories (no reconstruction needed -- porosityF and the
checked fields share the same decomposition), and checks:

  - --dims FIELD=D0 D1 D2 D3 D4 D5 D6   field's `dimensions` line matches
    exactly, across every processor and every value written.
  - --sign FIELD=positive|negative|zero  the mean of FIELD over cells where
    MASK < threshold satisfies the sign (zero requires every selected value
    to be exactly 0.0, not just a small mean -- the code paths this guards
    produce a hard zero, not a cancellation).

--threshold defaults to 0.9999, the criticalPorosity these fixtures set in
constant/pyrolysisProperties; pass it explicitly for a case that differs.

Usage:
  checkFieldSign.py <caseDir> [--mask FIELD] [--threshold VALUE]
                    [--dims FIELD=d0 d1 d2 d3 d4 d5 d6]...
                    [--sign FIELD=positive|negative|zero]...

Exit codes: 0 pass, 1 assertion failure, 2 infrastructure error.
"""

import argparse
import glob
import os
import re
import sys

DIMS_RE = re.compile(r"dimensions\s+\[\s*([^\]]*?)\s*\]")
NONUNIFORM_RE = re.compile(
    r"internalField\s+nonuniform\s+List<scalar>\s*\n?\s*(\d+)\s*\(\s*(.*?)\s*\)\s*;",
    re.S,
)
UNIFORM_RE = re.compile(r"internalField\s+uniform\s+([\-0-9.eE+]+)\s*;")


def read_field(path):
    """Return (dims_string, values) -- values is a list, or a bare float for
    a uniform field (the caller broadcasts it to cell count as needed)."""
    text = open(path).read()

    dims_match = DIMS_RE.search(text)
    if not dims_match:
        raise ValueError(f"no dimensions line found in {path}")
    dims = " ".join(dims_match.group(1).split())

    nonuniform = NONUNIFORM_RE.search(text)
    if nonuniform:
        n = int(nonuniform.group(1))
        vals = [float(x) for x in nonuniform.group(2).split()]
        if len(vals) != n:
            raise ValueError(
                f"{path}: header says {n} cells, parsed {len(vals)} values"
            )
        return dims, vals

    uniform = UNIFORM_RE.search(text)
    if uniform:
        return dims, float(uniform.group(1))

    raise ValueError(f"could not parse internalField in {path}")


def broadcast(value, n):
    if isinstance(value, list):
        if len(value) != n:
            raise ValueError(f"length mismatch: {len(value)} vs {n}")
        return value
    return [value] * n


def latest_time_dir(proc_dir):
    times = []
    for entry in os.listdir(proc_dir):
        path = os.path.join(proc_dir, entry)
        if not os.path.isdir(path):
            continue
        try:
            times.append((float(entry), entry))
        except ValueError:
            continue
    if not times:
        return None
    times.sort()
    # Skip time 0: lambdaDot's first coupling call zeroes it deliberately
    # (no previous Ts to difference against yet).
    nonzero = [t for t in times if t[0] > 0]
    return (nonzero or times)[-1][1]


def parse_field_arg(spec):
    field, _, rest = spec.partition("=")
    if not rest:
        raise argparse.ArgumentTypeError(f"expected FIELD=VALUE, got: {spec}")
    return field, rest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("case_dir")
    ap.add_argument("--mask", default="porosityF")
    ap.add_argument("--threshold", type=float, default=0.9999)
    ap.add_argument("--dims", action="append", default=[], metavar="FIELD=D0 .. D6")
    ap.add_argument("--sign", action="append", default=[],
                     metavar="FIELD=positive|negative|zero")
    args = ap.parse_args()

    case_dir = os.path.abspath(args.case_dir)
    proc_dirs = sorted(glob.glob(os.path.join(case_dir, "processor*")))
    if not proc_dirs:
        print(f"ERROR: no processor* directories found under {case_dir}",
              file=sys.stderr)
        return 2

    dims_checks = dict(parse_field_arg(s) for s in args.dims)
    sign_checks = dict(parse_field_arg(s) for s in args.sign)

    # Reject an unrecognised sign rather than silently asserting nothing:
    # neither branch below would match it and the case would pass unchecked.
    bad = {f: s for f, s in sign_checks.items()
           if s not in ("positive", "negative", "zero")}
    if bad:
        for field, sign in bad.items():
            print(f"ERROR: --sign {field}={sign}: expected one of "
                  "positive, negative, zero", file=sys.stderr)
        return 2

    needed_fields = set(dims_checks) | set(sign_checks) | {args.mask}

    dims_seen = {f: set() for f in dims_checks}
    values = {f: [] for f in sign_checks}
    mask_values = []

    for proc_dir in proc_dirs:
        time_dir = latest_time_dir(proc_dir)
        if time_dir is None:
            print(f"ERROR: no time directories under {proc_dir}", file=sys.stderr)
            return 2

        parsed = {}
        for field in needed_fields:
            path = os.path.join(proc_dir, time_dir, field)
            if not os.path.isfile(path):
                print(f"ERROR: {path} not found", file=sys.stderr)
                return 2
            try:
                parsed[field] = read_field(path)
            except ValueError as exc:
                print(f"ERROR: {exc}", file=sys.stderr)
                return 2

        for field in dims_checks:
            dims_seen[field].add(parsed[field][0])

        mask_raw = parsed[args.mask][1]
        n = len(mask_raw) if isinstance(mask_raw, list) else None
        for field in sign_checks:
            field_raw = parsed[field][1]
            if n is None:
                n = len(field_raw) if isinstance(field_raw, list) else 1
            field_vals = broadcast(field_raw, n)
            values[field].extend(field_vals)
        mask_values.extend(broadcast(mask_raw, n or 1))

    failures = []

    for field, expected in dims_checks.items():
        seen = dims_seen[field]
        if seen != {expected}:
            failures.append(
                f"{field}: expected dimensions [{expected}], saw {sorted(seen)}"
            )

    solid_mask = [m < args.threshold for m in mask_values]
    n_solid = sum(solid_mask)
    if sign_checks and n_solid == 0:
        failures.append(
            f"no cells with {args.mask} < {args.threshold} found across "
            f"{len(mask_values)} cells -- packed-bed box did not register"
        )
    else:
        for field, sign in sign_checks.items():
            solid_vals = [v for v, keep in zip(values[field], solid_mask) if keep]
            if sign == "zero":
                max_abs = max(abs(v) for v in solid_vals)
                print(f"  {field}: {n_solid} solid cells, max|value| = {max_abs:.6e}")
                if max_abs != 0.0:
                    failures.append(
                        f"{field}: expected exact zero over solid cells, "
                        f"max|value| = {max_abs:.6e}"
                    )
            else:
                mean_val = sum(solid_vals) / len(solid_vals)
                print(f"  {field}: {n_solid} solid cells, mean = {mean_val:.6e}")
                if sign == "positive" and mean_val <= 0.0:
                    failures.append(
                        f"{field}: expected positive mean over solid cells, "
                        f"got {mean_val:.6e}"
                    )
                elif sign == "negative" and mean_val >= 0.0:
                    failures.append(
                        f"{field}: expected negative mean over solid cells, "
                        f"got {mean_val:.6e}"
                    )

    if failures:
        for f in failures:
            print(f"ERROR: {f}", file=sys.stderr)
        return 1

    print("checkFieldSign.py: all dimension and sign checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
