"""
pgfViewMain -- the script pgfView hands to paraview / pvbatch.

The GUI's --script takes no arguments, so pgfView passes them in the
environment: PGFVIEW_CASE, PGFVIEW_COMPARE for --compare, and for --png
also PGFVIEW_PNG, PGFVIEW_TIME, PGFVIEW_SIZE.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pgfViews  # noqa: E402

case = pgfViews.Case(os.environ["PGFVIEW_CASE"])
other = os.environ.get("PGFVIEW_COMPARE")
if other:
    other = pgfViews.Case(other, needSystem=False)
    if other.label == case.label:
        case.label, other.label = case.name + " (A)", other.name + " (B)"
png = os.environ.get("PGFVIEW_PNG")
t = os.environ.get("PGFVIEW_TIME")
t = float(t) if t else None
if png:
    w, h = (int(x) for x in os.environ.get("PGFVIEW_SIZE", "1000x750").split("x"))
    if other:
        pgfViews.writeComparePngs(case, other, png, t, (w, h))
    else:
        pgfViews.writePngs(case, png, t, (w, h))
elif other:
    pgfViews.showCompareGui(case, other)
else:
    pgfViews.showGui(case)
