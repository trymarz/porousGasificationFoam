"""Post-run checks for LEIgasifierR3's feed and discharge: R2's checks.

Reads balance.txt (written by MPI_lambda.py) and the port flows, then checks:
  - solid mass in = out + shrinkage + inventory change, every row, to round-off;
  - the coupling's id list matches the spheres at every row;
  - the feed and discharge ran at the set rate (discharge backlog bounded);
  - the bed level settled: its range over the last half of the run;
  - the gas flow: R2's outflow = inflow check does not hold with pyrolysis
    gas, so it is reported, and checkHotBed.py closes the balance instead.

Usage, from the case directory after the coupled run:
    python3 checkFeed.py
"""
import glob
import os
import sys

sys.path.insert(0, os.getcwd())

import demGeometry as geom

tolMass   = 1e-12                    # kg, round-off on sums of ~1e-4 kg
tolLevel  = 4.0 * geom.sphere_radius  # m, two diameters over the last half
maxBacklog = 5                       # spheres the discharge may lag behind

rows = [list(map(float, l.split())) for l in open("balance.txt")
        if not l.startswith("#")]
if not rows:
    sys.exit("[FEED] balance.txt is empty")
(t, nSph, nCpl, nIn, nOut, nBurnt, mIn, mOut, mBurnt, inv, res, lvl, mShr,
 rMin) = zip(*rows)
failures = []

worst = max(abs(r) for r in res)
print("[FEED] %d rows to t = %.3f s; max |in - out - shrunk - d(inventory)| "
      "= %.2e kg; shrunk %.4e kg; smallest radius %.2f r0"
      % (len(rows), t[-1], worst, mShr[-1], min(rMin) / geom.sphere_radius))
if worst > tolMass:
    failures.append("mass balance residual %.2e kg" % worst)
if any(a != b for a, b in zip(nSph, nCpl)):
    failures.append("coupling ids and spheres differ")

expected = int(t[-1] * geom.feed_rate)
backlog = max(i - o for i, o in zip(nIn, nOut))
print("[FEED] in %d (expected %d), out %d, burnt %d; max backlog %d; "
      "inventory %.4f -> %.4f kg"
      % (nIn[-1], expected, nOut[-1], nBurnt[-1], backlog, inv[0], inv[-1]))
if abs(nIn[-1] - expected) > 1:
    failures.append("fed %d spheres, expected %d" % (nIn[-1], expected))
if backlog > maxBacklog:
    failures.append("discharge lagged by %d spheres" % backlog)

half = [l for tt, l in zip(t, lvl) if tt >= t[-1] / 2]
span = max(half) - min(half)
print("[FEED] bed level %.4f m at start, %.4f m at end; "
      "range over the last half %.4f m (%.2f r)"
      % (lvl[0], lvl[-1], span, span / geom.sphere_radius))
if span > tolLevel:
    failures.append("bed level moved %.4f m over the last half" % span)


def portFlow(name):
    files = sorted(glob.glob("postProcessing/%s/*/surfaceFieldValue.dat" % name))
    out = []
    for fn in files:
        out += [list(map(float, l.split()[:2])) for l in open(fn)
                if not l.startswith("#")]
    return dict(out)


flows = [portFlow(n) for n in ("portFlow", "primaryFlow", "secondaryFlow")]
if all(flows):
    times = [tt for tt in sorted(flows[0]) if tt >= t[-1] / 2
             and all(tt in f for f in flows)]
    rel = max(abs(sum(f[tt] for f in flows)) / abs(flows[1][tt] + flows[2][tt])
              for tt in times)
    print("[FLOW] max |out + in| / in over the last half: %.2e "
          "(pyrolysis gas; not checked)" % rel)
else:
    failures.append("no port flow in postProcessing/")

for f in failures:
    print("[FEED] FAIL: " + f)
print("[FEED] %s" % ("FAILED" if failures else "OK"))
sys.exit(1 if failures else 0)
