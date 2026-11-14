"""Post-run checks for LEIgasifierR4's O2-blown ignition window (README.md).

Reads the reconstructed write times, 0/V, 0/C, the per-step port flows and
the solver log, then checks:
  - bounds: Ts and gas T above T_cold and inside the JANAF range, mass
    fractions in [0, 1] summing to 1, no "bounding" or "clamped" line;
  - element balance: for C, H and O, inventory(t0) - inventory(t) - net port
    flow - inlet diffusion over (t0, t] is small next to what the solid
    released plus the O2 that came in (burnt char is part of the solid loss);
  - energy balance: total (formation + sensible) enthalpy of gas and solid,
    with the net port flow, the inlet conduction and diffusion and the
    pressure work, small next to the formation enthalpy the window moved;
  - reaction zone: the char ignites (its hottest cell above the grate is
    hotter than the strip started) and that cell sits next to the primary ports.
The drying and pyrolysis fronts are reported, as in R3.

Usage, from the case directory after the coupled run and reconstructPar:
    python3 checkIgnition.py
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.getcwd())

import hotBed as bed
import leiGeometry as lei

tolT      = 1.0     # K, slack on the lower temperature bound
tolY      = 1e-6    # mass-fraction slack
tolElem   = 1e-3    # element residual over (released + inflow)
tolEnergy = 1e-2    # energy residual over the formation enthalpy moved
zoneReach = 0.06    # m, hottest char cell to the nearest primary port face
zoneFrom  = 0.5     # s, the zone check starts here (ignition transient)

R = 8.314462618   # J/mol/K
Tstd = 298.15
W = dict(C=12.011, H=1.008, O=15.999, N=14.007)
GAS = dict(N2=dict(N=2), O2=dict(O=2), H2O=dict(H=2, O=1), CO=dict(C=1, O=1),
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


# ---------------------------------------------------------------------------
# Thermo: JANAF gas (thermo.compressibleGas), constant-Cp solid
# ---------------------------------------------------------------------------
def _janaf():
    txt = open("constant/thermo.compressibleGas").read()
    names = re.findall(r"^(\w+)\s*(?://.*)?\n\{", txt, re.M)
    parts = re.split(r"^\w+\s*(?://.*)?\n(?=\{)", txt, flags=re.M)[1:]
    out = {}
    for name, b in zip(names, parts):
        num = lambda k: float(re.search(k + r"\s+([\d.eE+-]+)", b).group(1))
        cf = lambda k: list(map(float, re.search(k + r"\s*\(([^)]*)\)", b)
                                .group(1).split()))
        out[name] = dict(W=num("molWeight") * 1e-3, Tc=num("Tcommon"),
                         Tlow=num("Tlow"), Thigh=num("Thigh"),
                         lo=cf("lowCpCoeffs"), hi=cf("highCpCoeffs"),
                         As=num("As"), Ts=num("Ts"))
    return out


JANAF = _janaf()


def Ha(s, T):
    """Absolute enthalpy, J/kg (OpenFOAM janafThermo)."""
    j = JANAF[s]
    a = j["lo"] if T < j["Tc"] else j["hi"]
    return R / j["W"] * (((((a[4] / 5 * T + a[3] / 4) * T + a[2] / 3) * T
                           + a[1] / 2) * T + a[0]) * T + a[5])


def Cp(s, T):
    j = JANAF[s]
    a = j["lo"] if T < j["Tc"] else j["hi"]
    return R / j["W"] * ((((a[4] * T + a[3]) * T + a[2]) * T + a[1]) * T + a[0])


HF = {s: Ha(s, Tstd) for s in GAS}


def mu(s, T):
    j = JANAF[s]
    return j["As"] * T ** 0.5 / (1.0 + j["Ts"] / T)


def kappa(s, T):
    """sutherlandTransport: modified Eucken."""
    Cv = Cp(s, T) - R / JANAF[s]["W"]
    return mu(s, T) * Cv * (1.32 + 1.77 * R / JANAF[s]["W"] / Cv)


def _solid_thermo():
    txt = open("constant/solidThermophysicalProperties").read()
    out = {}
    for s in SOLID:
        b = txt[txt.index(s + "Coeffs"):]
        b = b[:b.index("};")]
        out[s] = dict(Cp=float(re.search(r"\bCp\s+([\d.eE+-]+)", b).group(1)),
                      Hf=float(re.search(r"\bHf\s+([\d.eE+-]+)", b).group(1)))
    return out


SOLID_THERMO = _solid_thermo()


def h_solid(s, T):
    """constSolidThermo::h: Cp (T - Tstd) + Hf, J/kg."""
    p = SOLID_THERMO[s]
    return p["Cp"] * (T - Tstd) + p["Hf"]


failures = []


def fail(msg):
    failures.append(msg)
    print("[IGNITION] FAIL: " + msg)


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


def gas_state(d):
    p, T = field(d, "p"), field(d, "T")
    Y = {s: field(d, s) for s in GAS}
    rho = [p[c] / (R * T[c] * sum(Y[s][c] / MW[s] for s in GAS))
           for c in range(n)]
    return rho, Y, T, p


def inventory(d, t):
    """Element masses (kg) of solid and gas, and their enthalpies (J):
    total (formation + sensible) and formation only."""
    Ym = solid_masses(d, t)
    rho, Y, T, _ = gas_state(d)
    eps = field(d, "porosityF")
    Ts = field(d, "Ts")
    sol = {e: sum(V[c] * sum(Ym[s][c] * EF[s][e] for s in SOLID)
                  for c in range(n)) for e in ELEMENTS}
    gas = {e: sum(V[c] * eps[c] * rho[c] * sum(Y[s][c] * EF[s][e] for s in GAS)
                  for c in range(n)) for e in ELEMENTS}
    H = sum(V[c] * (eps[c] * rho[c] * sum(Y[s][c] * Ha(s, T[c]) for s in GAS)
                    + sum(Ym[s][c] * h_solid(s, Ts[c]) for s in SOLID))
            for c in range(n))
    Hf = sum(V[c] * (eps[c] * rho[c] * sum(Y[s][c] * HF[s] for s in GAS)
                     + sum(Ym[s][c] * SOLID_THERMO[s]["Hf"] for s in SOLID))
             for c in range(n))
    return sol, gas, H, Hf


PORTS = ("outlet", "primary", "secondary")


def port_rows(port):
    """[(t, {field: weighted sum})] through a port, one row per time step;
    phi-weighted, so inflow is negative."""
    rows = []
    pat = "postProcessing/%sSpecies/*/surfaceFieldValue.dat" % port
    for fn in sorted(glob.glob(pat)):
        head = None
        for line in open(fn):
            if line.startswith("#"):
                head = line[1:].split()
                continue
            v = list(map(float, line.split()))
            names = [re.sub(r"weightedSum\((\w+)\)", r"\1", h) for h in head[1:]]
            rows.append((v[0], dict(zip(names, v[1:]))))
    return sorted(rows)


def port_totals(rows, t0, t1):
    """Element kg, total and formation enthalpy J out over (t0, t1]."""
    el = {e: 0.0 for e in ELEMENTS}
    H = Hf = 0.0
    prev = None
    for t, f in rows:
        if prev is not None and t0 < t <= t1 + 1e-12:
            dt = t - prev
            for e in ELEMENTS:
                el[e] += dt * sum(f[s] * EF[s][e] for s in GAS)
            hf = sum(f[s] * HF[s] for s in GAS)
            Hf += dt * hf
            H += dt * (f["h"] + hf)
        prev = t
    return el, H, Hf


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


# The inlet faces hold pure O2 at T_IN (0.orig): its mu and alpha = kappa/Cp
# are the boundary diffusivities of YEqn (rho D = mu) and EEqn.
T_IN = 900.0
MU_IN = mu("O2", T_IN)
ALPHA_IN = kappa("O2", T_IN) / Cp("O2", T_IN)
Y_IN = {s: (1.0 if s == "O2" else 0.0) for s in GAS}


def inlet_diffusion(d, faces):
    """Out through the inlets by diffusion: element kg/s, and total and
    formation enthalpy W (species carry hf; EEqn conducts hs)."""
    _, Y, T, _ = gas_state(d)
    el = {e: 0.0 for e in ELEMENTS}
    H = Hf = 0.0
    hs_in = Ha("O2", T_IN) - HF["O2"]
    for c, w in faces:
        dY = {s: Y[s][c] - Y_IN[s] for s in GAS if s != "N2"}
        for e in ELEMENTS:
            el[e] += MU_IN * w * sum(dY[s] * EF[s][e] for s in dY)
        hf = MU_IN * w * sum(dY[s] * HF[s] for s in dY)
        hs_c = sum(Y[s][c] * (Ha(s, T[c]) - HF[s]) for s in GAS)
        Hf += hf
        H += hf + ALPHA_IN * w * (hs_c - hs_in)
    return el, H, Hf


def trapezoid(series, key=None):
    get = (lambda v: v) if key is None else (lambda v: v[key])
    return sum(0.5 * (b[0] - a[0]) * (get(a[1]) + get(b[1]))
               for a, b in zip(series, series[1:]))


# ---------------------------------------------------------------------------
# Bounds
# ---------------------------------------------------------------------------
ts = times()
T_MAX = min(j["Thigh"] for j in JANAF.values())
print("[IGNITION] write times: %s" % " ".join(d for _, d in ts))
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
    print("[IGNITION] t %-5s Ts %.1f..%.1f  T %.1f..%.1f  Y %.2e..%.6f "
          "(|sum-1| %.1e)  Ys %.2e..%.6f"
          % (d, min(Ts), max(Ts), min(T), max(T), gmin, gmax, gsum, smin, smax))
    lo = bed.T_cold - tolT
    if min(Ts) < lo or max(Ts) > T_MAX:
        fail("t = %s: Ts outside [%g, %g] K" % (d, lo, T_MAX))
    if min(T) < lo or max(T) > T_MAX:
        fail("t = %s: T outside [%g, %g] K" % (d, lo, T_MAX))
    if gmin < -tolY or gmax > 1 + tolY or gsum > tolY:
        fail("t = %s: gas mass fractions out of bounds" % d)
    if smin < -tolY or smax > 1 + tolY:
        fail("t = %s: solid mass fractions out of bounds" % d)

logs = [f for f in glob.glob("log.*") if os.path.isfile(f)]
bounding = [l for f in logs for l in open(f, errors="replace")
            if re.search(r"^\s*bounding \w+,|clamped", l)]
print("[IGNITION] logs %s: %d bounding/clamp lines"
      % (" ".join(sorted(logs)) or "(none)", len(bounding)))
if not logs:
    fail("no log.* to scan for bounding")
if bounding:
    fail("solver bounded or clamped a field: %s" % bounding[0].strip())

# ---------------------------------------------------------------------------
# Element and energy balances over the window
# ---------------------------------------------------------------------------
rows = {p: port_rows(p) for p in PORTS}
if not all(rows.values()):
    fail("missing postProcessing/*Species for %s"
         % [p for p in PORTS if not rows[p]])
else:
    (t0, d0), (t1, d1) = ts[0], ts[-1]
    s0, g0, H0, Hf0 = inventory(d0, t0)
    s1, g1, H1, Hf1 = inventory(d1, t1)
    port = {p: port_totals(rows[p], t0, t1) for p in PORTS}
    net = {e: sum(port[p][0][e] for p in PORTS) for e in ELEMENTS}
    inflow = {e: -sum(port[p][0][e] for p in PORTS if p != "outlet")
              for e in ELEMENTS}
    # Trapezoid over the write times: the only term not summed per step.
    faces = inlet_faces()
    jd = [(t, inlet_diffusion(d, faces)) for t, d in ts]
    diff = {e: trapezoid([(t, j[0]) for t, j in jd], e) for e in ELEMENTS}
    print("[IGNITION] element balance over %g .. %g s (kg):" % (t0, t1))
    for e in ELEMENTS:
        released = s0[e] - s1[e]
        res = (s0[e] + g0[e]) - (s1[e] + g1[e]) - net[e] - diff[e]
        scale = released + inflow[e]
        rel = abs(res) / scale if scale > 0 else float("inf")
        print("[IGNITION]   %s: solid %.6e -> %.6e (released %.4e), gas %.4e "
              "-> %.4e, in %.4e, out %.4e, inlet diffusion %.3e, residual "
              "%.2e (%.2e of released + in)"
              % (e, s0[e], s1[e], released, g0[e], g1[e], inflow[e],
                 port["outlet"][0][e], diff[e], res, rel))
        if rel > tolElem:
            fail("%s balance residual %.2e of released + in" % (e, rel))

    H_net = sum(port[p][1] for p in PORTS)
    Hf_net = sum(port[p][2] for p in PORTS)
    H_diff = trapezoid([(t, j[1]) for t, j in jd])
    Hf_diff = trapezoid([(t, j[2]) for t, j in jd])
    # EEqn solves sensible h with -porosityF dp/dt: the pressure work enters.
    eps = field(d0, "porosityF")
    pw = sum(V[c] * eps[c] * (b - a) for c, (a, b) in
             enumerate(zip(field(d0, "p"), field(d1, "p"))))
    res = (H0 - H1) - H_net - H_diff + pw
    # Heat released (net), and the formation enthalpy moved (the scale).
    Q = (Hf0 - Hf1) - Hf_net - Hf_diff
    scale = abs(Hf0 - Hf1) + abs(Hf_net) + abs(Hf_diff)
    print("[IGNITION] energy balance over %g .. %g s (J): enthalpy %.8e -> "
          "%.8e, net port flow %.4e, inlet conduction + diffusion %.3e, "
          "pressure work %.3e" % (t0, t1, H0, H1, H_net, H_diff, pw))
    print("[IGNITION]   net reaction heat %.4e J (%.3e W per slab); residual "
          "%.3e J, %.2e of the formation enthalpy moved (%.4e J)"
          % (Q, Q / (t1 - t0), res, abs(res) / scale, scale))
    if abs(res) / scale > tolEnergy:
        fail("energy residual %.2e of the formation enthalpy moved"
             % (abs(res) / scale))

    char = [sum(V[c] * m for c, m in enumerate(solid_masses(d, t)["char"]))
            for t, d in ((t0, d0), (t1, d1))]
    wood = [sum(V[c] * m for c, m in enumerate(solid_masses(d, t)["wood"]))
            for t, d in ((t0, d0), (t1, d1))]
    o2 = -sum(port[p][0]["O"] for p in ("primary", "secondary")) \
        * MW["O2"] / (2 * W["O"] * 1e-3)
    o2_out = sum(f["O2"] * (t - tp) for (tp, _), (t, f)
                 in zip(rows["outlet"], rows["outlet"][1:]) if t0 < t <= t1)
    gasO2 = []
    for d in (d0, d1):
        rho, Y, _, _ = gas_state(d)
        e_ = field(d, "porosityF")
        gasO2.append(sum(V[c] * e_[c] * rho[c] * Y["O2"][c] for c in range(n)))
    print("[IGNITION] char %.4e -> %.4e kg, wood pyrolysed %.4e kg; O2 in "
          "%.4e kg, out %.4e kg (%.1f %% slip), held in the pores %.4e kg, "
          "consumed %.4e kg" % (char[0], char[1], wood[0] - wood[1], o2,
          o2_out, 100.0 * o2_out / o2 if o2 > 0 else 0.0, gasO2[1] - gasO2[0],
          o2 - o2_out - (gasO2[1] - gasO2[0])))

# ---------------------------------------------------------------------------
# Reaction zone
# ---------------------------------------------------------------------------
ports = bed.port_centres()


def port_distance(x, z):
    return min(((x - px) ** 2 + (z - pz) ** 2) ** 0.5 for px, pz in ports)


print("[IGNITION] hottest char above the grate (height above the ramp's "
      "foot; distance to the nearest primary port face):")
for t, d in ts:
    Ts = field(d, "Ts")
    Yc = field(d, "Ychar")
    present = field(d, "solidPresent") if t > 0 else [1.0] * n
    Y_O2 = field(d, "O2")
    cand = [c for c in range(n) if present[c] > 0.5 and Yc[c] > 0.5
            and C[c][2] > lei.z_grate]
    c = max(cand, key=lambda i: Ts[i])
    dist = port_distance(C[c][0], C[c][2])
    reach = [port_distance(C[k][0], C[k][2]) for k in range(n)
             if Y_O2[k] > 0.5 and C[k][2] > lei.z_grate]
    print("[IGNITION]   t %-5s Ts %.0f K at %+.0f mm, %.0f mm from a port; "
          "Y_O2 > 0.5 up to %.0f mm from a port"
          % (d, Ts[c], 1e3 * (C[c][2] - bed.z_hot), 1e3 * dist,
             1e3 * max(reach) if reach else 0.0))
    if t >= zoneFrom:
        if Ts[c] <= bed.T_ign:
            fail("t = %s: char not ignited (hottest %.0f K, strip started at "
                 "%.0f K)" % (d, Ts[c], bed.T_ign))
        elif dist > zoneReach:
            fail("t = %s: hottest char %.0f mm from the primary ports"
                 % (d, 1e3 * dist))

# ---------------------------------------------------------------------------
# Drying and pyrolysis fronts in the shaft (reported, as R3)
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
    if not zp:
        return None, None
    return sum(zp) / len(zp), sum(zd) / len(zd)


print("[IGNITION] fronts above the ramp's foot (column mean of the 0.5 "
      "crossing):")
for t, d in ts:
    zp, zd = fronts(d)
    if zp is None:
        print("[IGNITION]   t %-5s no front in the shaft" % d)
        continue
    print("[IGNITION]   t %-5s pyrolysis (X = 0.5) %.1f mm   drying (M = 0.5) "
          "%.1f mm" % (d, 1e3 * (zp - bed.z_hot), 1e3 * (zd - bed.z_hot)))
    if zd < zp:
        fail("t = %s: drying front below the pyrolysis front" % d)

print("[IGNITION] %s" % ("FAILED" if failures else "OK"))
sys.exit(1 if failures else 0)
