"""
pgfViewMain -- the script pgfView hands to paraview / pvbatch.

The GUI's --script takes no arguments, so pgfView passes them in the
environment: PGFVIEW_CASE, and for --png also PGFVIEW_PNG, PGFVIEW_TIME,
PGFVIEW_SIZE.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pgfViews  # noqa: E402

case = pgfViews.Case(os.environ["PGFVIEW_CASE"])
png = os.environ.get("PGFVIEW_PNG")
if png:
    t = os.environ.get("PGFVIEW_TIME")
    w, h = (int(x) for x in os.environ.get("PGFVIEW_SIZE", "1000x750").split("x"))
    pgfViews.writePngs(case, png, None if not t else float(t), (w, h))
else:
    pgfViews.showGui(case)
