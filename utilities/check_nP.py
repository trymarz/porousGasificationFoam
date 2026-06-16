#!/usr/bin/env python3
"""Quick-verify the nParticles field for the cell-0 overwrite bug.

Usage: python3 utilities/check_nP.py processor0/<time>/nParticles

Reads the internalField values of an OpenFOAM scalar field and reports
cells/sum/min/max. The cell-0 overwrite bug shows up as max == 320 with a
single cell holding the phantom accumulation; a healthy field has max ~5-7
and sum ~320.
"""
import sys


def read_field(fp):
    with open(fp) as f:
        in_data, vals = False, []
        for line in f:
            if 'internalField' in line:
                in_data = True
                continue
            if in_data:
                s = line.strip()
                if s == ';' or s.startswith(')'):
                    break
                if s:
                    try:
                        vals.extend(
                            float(x)
                            for x in s.replace('(', '').replace(')', '').split()
                        )
                    except ValueError:
                        pass
        return vals


v = read_field(sys.argv[1])
print(f"cells={len(v)} sum={sum(v):.0f} min={min(v):.0f} max={max(v):.0f}")
if max(v) == 320 and any(vi > 10 for vi in v):
    print(f"FAIL: cell-0 accumulation (max={max(v)})")
else:
    print("PASS")
