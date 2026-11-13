"""Post-run checks for LEIgasifierR3's hot, oxygen-free window (README.md).

Reads the reconstructed write times, 0/V, 0/C, the outlet species flows and
the solver log, then checks:
  - bounds: Ts within [T_cold, T_hot] (no heat source: drying and pyrolysis
    take heat, hr > 0), gas T likewise but with tolTgas above T_hot (README,
    "Checks"), mass fractions in [0, 1] summing to 1, no "bounding" line;
  - element balance: for C, H and O, inventory(t0) - inventory(t) - outflow
    - inlet diffusion over (t0, t] is small next to what the solid released;
  - the drying and pyrolysis fronts: their height in the shaft at every write
    time, their speed, and that drying stays above pyrolysis.

Usage, from the case directory after the coupled run and reconstructPar:
    python3 checkHotBed.py
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.getcwd())

import hotBed as bed
import leiGeometry as lei

tolT    = 1.0     # K, slack on the temperature bounds
tolTgas = 9.0     # K, gas T over T_hot (1 %); open point in README "Checks"
tolY    = 1e-6    # mass-fraction slack
tolElem = 1e-3    # element residual over the element mass the solid released

R = 8.314462618   # J/mol/K
W = dict(C=12.011, H=1.008, O=15.999, N=14.007)
GAS = dict(N2=dict(N=2), H2O=dict(H=2, O=1), CO=dict(C=1, O=1),
           CO2=dict(C=1, O=2), H2=dict(H=2), CH4=dict(C=1, H=4),
           tar=dict(C=6, H=6))   # tar is benzene (thermo.compressibleGas)
SOLID = ("wood", "char", "moisture")
ELEMENTS = ("C", "H", "O")


def molar(f):
    return sum(W[e] * n for e, n in f.items())


def frac(f):
    """Element mass fractions of a formula."""
    m = molar(f)
    return {e: W[e] * f.get(e, 0) / m for e in ELEMENTS}


def pyrolysis_products():
    """Mass coefficients of 'wood = ...' in constant/chemistryProperties."""
    txt = open("constant/chemistryProperties").read()
    rhs = re.search(r"^\s*wood\s*=\s*(.+)$", txt, re.M).group(1)
    out = {}
    for term in rhs.split("+"):
        c, s = term.split()
        out[s] = float(c)
    return out


# Element fractions of the solids. Wood's are defined by its products, so the
# reaction closes by construction; the run must not lose what it moves.
EF = {s: frac(f) for s, f in GAS.items()}
EF["char"] = frac(dict(C=1))
EF["moisture"] = frac(dict(H=2, O=1))
prod = pyrolysis_products()
EF["wood"] = {e: sum(c * EF[s][e] for s, c in prod.items()) for e in ELEMENTS}
MW = {s: molar(f) * 1e-3 for s, f in GAS.items()}


failures = []


def fail(msg):
    failures.append(msg)
    print("[HOTBED] FAIL: " + msg)


def times():
    ts = []
    for d in os.listdir("."):
        try:
            t = float(d)
        except ValueError:
            continue
        if os.path.isdir(d) and os.path.exists(os.path.join(d, "Ts")):
            ts.append((t, d))
    return sorted(ts)


V = bed.read_internal("0/V")
C = bed.read_internal("0/C")
n = len(V)


def field(d, name):
    path = os.path.join(d, name)
    if not os.path.exists(path) and name in GAS:
        return [0.0] * n   # 0/ takes these from Ydefault: none at the start
    return bed.read_internal(path, n)


def solid_masses(d, t):
    """kg/m3 of cell per solid specie; at t = 0 from Ys, rhos and porosity."""
    if t > 0:
        return {s: field(d, "Y%sm" % s) for s in SOLID}
    Ys = {s: field(d, "Y" + s) for s in SOLID}
    eps = field(d, "porosityF")
    out = {s: [] for s in SOLID}
    for c in range(n):
        inv = sum(Ys[s][c] / bed.rho[s] for s in SOLID)
        rhos = 1.0 / inv if inv > 0 else 0.0
        for s in SOLID:
            out[s].append(Ys[s][c] * rhos * (1.0 - eps[c]))
    return out


def gas_density(d):
    p, T = field(d, "p"), field(d, "T")
    Y = {s: field(d, s) for s in GAS}
    return [p[c] / (R * T[c] * sum(Y[s][c] / MW[s] for s in GAS))
            for c in range(n)], Y


def inventory(d, t):
    """(solid, gas) element masses (kg) in the domain."""
    Ym = solid_masses(d, t)
    rho, Y = gas_density(d)
    eps = field(d, "porosityF")
    sol = {e: sum(V[c] * sum(Ym[s][c] * EF[s][e] for s in SOLID)
                  for c in range(n)) for e in ELEMENTS}
    gas = {e: sum(V[c] * eps[c] * rho[c] * sum(Y[s][c] * EF[s][e] for s in GAS)
                  for c in range(n)) for e in ELEMENTS}
    return sol, gas


def outflow():
    """[(t, {specie: kg/s})] through the outlet, one row per time step."""
    rows = []
    for fn in sorted(glob.glob("postProcessing/outletSpecies/*/surfaceFieldValue.dat")):
        head = None
        for line in open(fn):
            if line.startswith("#"):
                head = line[1:].split()
                continue
            v = list(map(float, line.split()))
            names = [re.sub(r"weightedSum\((\w+)\)", r"\1", h) for h in head[1:]]
            rows.append((v[0], dict(zip(names, v[1:]))))
    return sorted(rows)


def _list(path):
    """Entries of a polyMesh list file, as strings."""
    txt = open(path).read().split("FoamFile", 1)[1].split("}", 1)[1]
    head = re.search(r"^\s*(\d+)\s*\(", txt, re.M)
    return re.findall(r"\d*\(([^()]*)\)", txt[head.end() - 1:])[:int(head.group(1))]


def inlet_faces(patches=("primaryInlet", "secondaryInlet")):
    """[(owner cell, |Sf| / (n . d))] of the inlet faces, d face - cell centre."""
    pm = "constant/polyMesh"
    pts = [tuple(map(float, e.split())) for e in _list(pm + "/points")]
    faces = [list(map(int, e.split())) for e in _list(pm + "/faces")]
    own = list(map(int, re.findall(r"\d+", open(pm + "/owner").read()
                     .split("FoamFile", 1)[1].split("}", 1)[1].split("(", 1)[1])))
    bnd = open(pm + "/boundary").read()
    out = []
    for p in patches:
        m = re.search(p + r"\s*\{[^}]*?nFaces\s+(\d+);\s*startFace\s+(\d+);", bnd)
        for f in range(int(m.group(2)), int(m.group(2)) + int(m.group(1))):
            q = [pts[i] for i in faces[f]]
            cf = [sum(v[k] for v in q) / len(q) for k in range(3)]
            a = [q[2][k] - q[0][k] for k in range(3)]
            b = [q[3][k] - q[1][k] for k in range(3)]
            S = [0.5 * (a[1] * b[2] - a[2] * b[1]), 0.5 * (a[2] * b[0] - a[0] * b[2]),
                 0.5 * (a[0] * b[1] - a[1] * b[0])]
            magS = sum(v * v for v in S) ** 0.5
            d = [cf[k] - C[own[f]][k] for k in range(3)]
            nd = abs(sum(S[k] * d[k] for k in range(3))) / magS
            out.append((own[f], magS / nd))
    return out


T_IN = 900.0   # K, inlet T (0.orig/T); the face gas is pure N2 at it
MU_IN = 1.67212e-06 * T_IN ** 0.5 / (1.0 + 170.672 / T_IN)   # N2 Sutherland


def inlet_diffusion(d, faces):
    """Element kg/s diffusing out through the inlets (YEqn: rho D = muEff = mu).
    The inlet faces hold Y = 0 for the products, so the flux is mu Y_cell."""
    Y = {s: field(d, s) for s in GAS if s != "N2"}
    return {e: sum(MU_IN * w * sum(Y[s][c] * EF[s][e] for s in Y)
                   for c, w in faces) for e in ELEMENTS}


def out_elements(rows, t0, t1):
    tot = {e: 0.0 for e in ELEMENTS}
    prev = None
    for t, f in rows:
        if prev is not None and t0 < t <= t1 + 1e-12:
            for e in ELEMENTS:
                tot[e] += (t - prev) * sum(f[s] * EF[s][e] for s in GAS)
        prev = t
    return tot


# ---------------------------------------------------------------------------
# Bounds
# ---------------------------------------------------------------------------
ts = times()
print("[HOTBED] write times: %s" % " ".join(d for _, d in ts))
for t, d in ts:
    present = field(d, "solidPresent") if t > 0 else [1.0] * n
    Ts = [v for v, p in zip(field(d, "Ts"), present) if p > 0.5]
    T = field(d, "T")
    Yg = {s: field(d, s) for s in GAS}
    Ysol = {s: field(d, "Y" + s) for s in SOLID}
    gmin = min(min(v) for v in Yg.values())
    gmax = max(max(v) for v in Yg.values())
    gsum = max(abs(sum(Yg[s][c] for s in GAS) - 1.0) for c in range(n))
    smin = min(min(v) for v in Ysol.values())
    smax = max(max(v) for v in Ysol.values())
    print("[HOTBED] t %-5s Ts %.1f..%.1f  T %.1f..%.1f  Y %.2e..%.6f "
          "(|sum-1| %.1e)  Ys %.2e..%.6f"
          % (d, min(Ts), max(Ts), min(T), max(T), gmin, gmax, gsum, smin, smax))
    lo, hi = bed.T_cold - tolT, bed.T_hot + tolT
    if min(Ts) < lo or max(Ts) > hi:
        fail("t = %s: Ts outside [%g, %g] K" % (d, lo, hi))
    if min(T) < lo or max(T) > bed.T_hot + tolTgas:
        fail("t = %s: T outside [%g, %g] K" % (d, lo, bed.T_hot + tolTgas))
    if max(T) > hi:
        c = max(range(n), key=lambda i: T[i])
        print("[HOTBED]   T above T_hot: %d cells, max %.1f K at (%.3f, %.3f), "
              "porosityF %.4f" % (sum(v > hi for v in T), T[c], C[c][0],
                                  C[c][2], field(d, "porosityF")[c]))
    if gmin < -tolY or gmax > 1 + tolY or gsum > tolY:
        fail("t = %s: gas mass fractions out of bounds" % d)
    if smin < -tolY or smax > 1 + tolY:
        fail("t = %s: solid mass fractions out of bounds" % d)

logs = [f for f in glob.glob("log.*") if os.path.isfile(f)]
bounding = [l for f in logs for l in open(f, errors="replace")
            if re.search(r"^\s*bounding \w+,|clamped", l)]
print("[HOTBED] logs %s: %d bounding/clamp lines"
      % (" ".join(sorted(logs)) or "(none)", len(bounding)))
if not logs:
    fail("no log.* to scan for bounding")
if bounding:
    fail("solver bounded or clamped a field: %s" % bounding[0].strip())

# ---------------------------------------------------------------------------
# Element balance over the window
# ---------------------------------------------------------------------------
rows = outflow()
if not rows:
    fail("no postProcessing/outletSpecies")
else:
    (t0, d0), (t1, d1) = ts[0], ts[-1]
    s0, g0 = inventory(d0, t0)
    s1, g1 = inventory(d1, t1)
    out = out_elements(rows, t0, t1)
    # Trapezoid over the write times: the inlet cells change slowly.
    faces = inlet_faces()
    jd = [(t, inlet_diffusion(d, faces)) for t, d in ts]
    diff = {e: sum(0.5 * (b[0] - a[0]) * (a[1][e] + b[1][e])
                   for a, b in zip(jd, jd[1:])) for e in ELEMENTS}
    print("[HOTBED] element balance over %g .. %g s (kg):" % (t0, t1))
    for e in ELEMENTS:
        released = s0[e] - s1[e]
        res = (s0[e] + g0[e]) - (s1[e] + g1[e]) - out[e] - diff[e]
        rel = abs(res) / released if released > 0 else float("inf")
        print("[HOTBED]   %s: solid %.6e -> %.6e (released %.4e), gas %.4e -> "
              "%.4e, out %.4e, inlet diffusion %.3e, residual %.2e (%.2e of "
              "released)" % (e, s0[e], s1[e], released, g0[e], g1[e], out[e],
                             diff[e], res, rel))
        if rel > tolElem:
            fail("%s balance residual %.2e of released" % (e, rel))
    wood = sum(V[c] * m for c, m in enumerate(solid_masses(d1, t1)["wood"]))
    wood0 = sum(V[c] * m for c, m in enumerate(solid_masses(d0, t0)["wood"]))
    print("[HOTBED] wood pyrolysed %.4e kg (%.3e kg/s per slab)"
          % (wood0 - wood, (wood0 - wood) / (t1 - t0)))

# ---------------------------------------------------------------------------
# Fronts in the shaft
# ---------------------------------------------------------------------------
fresh_ratio = bed.moisture / (1.0 - bed.moisture)


def fronts(d):
    """Mean over shaft columns of the highest X = 0.5 / M = 0.5 crossing,
    interpolated between cell centres so sub-cell motion shows."""
    Yw, Yc, Ym = (field(d, "Y" + s) for s in SOLID)
    cols = {}
    for c, (x, _, z) in enumerate(C):
        if z <= lei.z_plate or abs(x) >= lei.r_shaft:
            continue
        wood_eq = Yw[c] + Yc[c] / bed.char_yield   # fresh dry wood it came from
        if wood_eq <= 0:
            continue
        X = Yc[c] / bed.char_yield / wood_eq
        M = Ym[c] / (fresh_ratio * wood_eq)
        cols.setdefault(round(x, 4), []).append((z, X, M))
    def crossing(col, k, above):
        """Highest z where value k crosses 0.5 with 'above' below it."""
        for (z0, *a), (z1, *b) in reversed(list(zip(col, col[1:]))):
            v0, v1 = a[k], b[k]
            if (v0 > 0.5) == above and (v1 > 0.5) != above:
                return z0 + (0.5 - v0) / (v1 - v0) * (z1 - z0)
        return None
    zp, zd = [], []
    for col in cols.values():
        col.sort()
        p_, d_ = crossing(col, 0, True), crossing(col, 1, False)
        if p_ is not None and d_ is not None:
            zp.append(p_)
            zd.append(d_)
    return sum(zp) / len(zp), sum(zd) / len(zd)


print("[HOTBED] fronts in the shaft (column mean of the 0.5 crossing):")
hist = []
for t, d in ts:
    zp, zd = fronts(d)
    hist.append((t, zp, zd))
    print("[HOTBED]   t %-5s pyrolysis (X = 0.5) %.4f m   drying (M = 0.5) %.4f m"
          % (d, zp, zd))
    if zd < zp:
        fail("t = %s: drying front below the pyrolysis front" % d)
    if not bed.z_hot <= zp <= bed.z_hot + bed.ramp:
        fail("t = %s: pyrolysis front left the hot bed's ramp" % d)


def slope(pts):
    k = len(pts)
    mt = sum(p[0] for p in pts) / k
    mz = sum(p[1] for p in pts) / k
    den = sum((p[0] - mt) ** 2 for p in pts)
    return sum((p[0] - mt) * (p[1] - mz) for p in pts) / den if den else 0.0


if len(hist) > 2:
    print("[HOTBED] front speed (least squares, + is up): pyrolysis %.2f mm/s, "
          "drying %.2f mm/s" % (1e3 * slope([(t, zp) for t, zp, _ in hist]),
                                1e3 * slope([(t, zd) for t, _, zd in hist])))
    print("[HOTBED] (the hot bed was built for a %.2f mm/s descent; a steady "
          "front moves up through the solid at that speed)" % (1e3 * bed.v_solid))

print("[HOTBED] %s" % ("FAILED" if failures else "OK"))
sys.exit(1 if failures else 0)
