#!/usr/bin/env python3
"""Per-step closed-form energy audit of a one-cell interface case (#312).

    checkInterfaceEnergy.py [--tol K] [--case DIR] [--both-ways SPECIES]

The case: one cell, one open face, no interphase convection (h = 0), no
radiation, no gas reaction, no pressure work (dpdt no), ddt Euler, constant-Cp
solids, mass-based (non-stoichiometric) solid reactions each with its own lone
solid substrate. Each step's reaction rates r_k are read back from the solid
substrates' Y<solid>m change (write at writePrecision 15: the change is small
against Y<solid>m); everything else is recomputed by hand from the case's
input files, with the step-start Ts, Tg (as the solver does):

  S_i       = sum_k nu_ik r_k                net gas source of species i
  Q_prod    = sum_{S_i>0} S_i hs_i(Ts)               gasProductEnthalpyAtTs
  Q_react   = sum_{S_i<0} S_i hs_i(Tg)               gasReactantEnthalpyAtTg
  Q_int,g   = Q_prod + Q_react
  Q_heat    = sum_{S_i<0} |S_i| (hs_i(Ts) - hs_i(Tg))  heatUpGasReactantsToTs
  S_chem    = -sum_j RRs_j h_j(Ts) - sum_i S_i Ha_i(Ts)    (enthalpy mode)
            = 0                                             (hr mode, hr = 0)

  gas:   (m + dt Srho) hs_mix(Tg', Y') = m hs_mix(Tg, Y) + dt Q_int,g
         (m + dt Srho) Y_i'            = m Y_i + dt S_i,      m = porosityF rho
  solid: rhoCp_pre Ts' = H_old + dt (S_chem - Q_heat),  rhoCp_pre = sum Ym_j Cp_j

S_i is the NET source: a species both made and consumed in the cell is a
product if S_i > 0 and a reactant if S_i < 0, never both (#312 E6).

Checked per step: Tg' and Ts' against the closed form within TOL [K]; Y' within
1e-6; the written Srho, gasProductEnthalpyAtTs, gasReactantEnthalpyAtTg, their sum
(Q_int,g), heatUpGasReactantsToTs (and solidChemistrySh in enthalpy mode)
against the hand values; and the total
    dE_gas + dE_solid = dt (Q_int,g - Q_heat + S_chem) = dt (sum_i S_i hs_i(Ts) + S_chem)
from the measured temperatures. In enthalpy mode also the absolute enthalpy
(formation included, solid h = Cp (T - Tstd) + Hf): its change is
dt sum_j RRs_j Cp_j (Ts' - Ts), the converted solid's sensible heat between
the step-start Ts (S_chem) and the step-end Ts (solid store); zero for dt -> 0.
"""
import argparse, pathlib, re, sys

RR = 1e3 * 6.0221417930e+23 * 1.38065e-23  # J/kmol/K: 1e3*NA*k, OpenFOAM v2406
TSTD = 298.15
YTOL = 1e-6
FTOL = 1e-7  # relative, written fields against the hand values
ETOL = 1e-6  # relative, energy totals against the exchanged energy


def strip_comments(t):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", t, flags=re.S))


def internal(path):
    t = path.read_text()
    m = re.search(r"internalField\s+uniform\s+([^;]+);", t)
    if not m:
        m = re.search(r"internalField\s+nonuniform\s+List<scalar>\s*1\s*\(\s*([^)\s]+)\s*\)", t)
    if not m:
        sys.exit(f"{path}: cannot read a one-cell internalField")
    return float(m.group(1))


def entry(text, key):
    m = re.search(rf"^\s*{key}\s+([^;]+);", text, re.M)
    return m.group(1).strip() if m else None


def janaf(case):
    t = "\n" + strip_comments((case / "constant" / "thermo.compressibleGas").read_text())
    sp = {}
    for m in re.finditer(r"\n(\w+)\s*\{(.*?)\n\}", t, re.S):
        if m.group(1) == "FoamFile":
            continue
        body = m.group(2)
        W = float(re.search(r"molWeight\s+([^;]+);", body).group(1))
        Tc = float(re.search(r"Tcommon\s+([^;]+);", body).group(1))
        hi = [float(x) for x in re.search(r"highCpCoeffs\s*\(([^)]*)\)", body).group(1).split()]
        lo = [float(x) for x in re.search(r"lowCpCoeffs\s*\(([^)]*)\)", body).group(1).split()]
        sp[m.group(1)] = (W, Tc, hi, lo)
    return sp


def ha(c, T):
    W, Tc, hi, lo = c
    a = lo if T < Tc else hi
    return RR / W * (((((a[4] / 5 * T + a[3] / 4) * T + a[2] / 3) * T + a[1] / 2) * T + a[0]) * T + a[5])


def hs(c, T):
    return ha(c, T) - ha(c, TSTD)


def solids(case):
    """{name: (Cp, Hf, rho)} from inline <name>Coeffs blocks (no #include)."""
    t = strip_comments((case / "constant" / "solidThermophysicalProperties").read_text())
    names = re.search(r"solidComponents\s*\(([^)]*)\)", t).group(1).split()
    out = {}
    for n in names:
        m = re.search(rf"{n}Coeffs\s*\{{(.*?)\n\}}", t, re.S)
        if not m or "Cp" not in m.group(1):
            sys.exit(f"{n}Coeffs: needs an inline thermodynamics {{ Cp ..; Hf ..; }}")
        b = m.group(1)
        out[n] = (float(re.search(r"Cp\s+([^;]+);", b).group(1)),
                  float(re.search(r"Hf\s+([^;]+);", b).group(1)),
                  float(re.search(r"rho\s+([^;]+);", b).group(1)))
    return out


def side(expr):
    terms = []
    for term in expr.split("+"):
        f = term.split()
        terms.append((float(f[0]), f[1]) if len(f) == 2 else (1.0, f[0]))
    return terms


def reactions(case, solid_names, gas_names):
    t = strip_comments((case / "constant" / "chemistryProperties").read_text())
    if entry(t, "stoichiometricReactions") not in (None, "false", "off", "no"):
        sys.exit("needs stoichiometricReactions false (mass-based coefficients)")
    enthalpy = entry(t, "solidReactionEnergyFromEnthalpy") in (None, "true", "on", "yes")
    body = re.search(r"solidReactions\s*\((.*)\)\s*;", t, re.S).group(1)
    out = []
    for eq, coeffs in re.findall(r"Reaction\s+([^\n(]+?)\s*\(([^)]*)\)", body):
        lhs, rhs = (side(s) for s in eq.split("="))
        hr = float(coeffs.split()[3])
        if not enthalpy and hr != 0:
            sys.exit(f"{eq}: hr mode is checked only with hr = 0")
        subs = [n for _, n in lhs if n in solid_names]
        if len(subs) != 1:
            sys.exit(f"{eq}: needs exactly one solid substrate")
        nu = {}
        for c, n in lhs:
            nu[n] = nu.get(n, 0.0) - c
        for c, n in rhs:
            nu[n] = nu.get(n, 0.0) + c
        for n in nu:
            if n not in solid_names and n not in gas_names:
                sys.exit(f"{eq}: unknown species {n}")
        out.append((eq.strip(), subs[0], nu))
    substrates = [s for _, s, _ in out]
    if len(set(substrates)) != len(substrates):
        sys.exit("each reaction needs its own solid substrate (rates are read from it)")
    return enthalpy, out


def solid_mass(d, sol):
    """Y<j>m, or at the start time (not written yet) (1 - porosityF) rho_mix Y<j>."""
    if all((d / f"Y{j}m").exists() for j in sol):
        return {j: internal(d / f"Y{j}m") for j in sol}
    Y = {j: internal(d / f"Y{j}") for j in sol}
    rho = 1 / sum(Y[j] / sol[j][2] for j in sol)
    return {j: (1 - internal(d / "porosityF")) * rho * Y[j] for j in sol}


def solve_T(sp, Y, h, T0):
    T = T0
    for _ in range(100):
        f = sum(Y[s] * hs(sp[s], T) for s in Y) - h
        cp = sum(Y[s] * (hs(sp[s], T + 1e-3) - hs(sp[s], T - 1e-3)) for s in Y) / 2e-3
        dT = -f / cp
        T += dT
        if abs(dT) < 1e-12 * T:
            return T
    sys.exit("solve_T did not converge")


def rel(a, b, scale):
    return abs(a - b) / max(abs(scale), 1e-300)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tol", type=float, default=1e-5, help="temperature tolerance [K]")
    ap.add_argument("--case", default=".")
    ap.add_argument("--both-ways", metavar="SPECIES",
                    help="require SPECIES to be both made and consumed in every step")
    a = ap.parse_args()
    case = pathlib.Path(a.case)
    sp, sol = janaf(case), solids(case)
    times = sorted((d for d in case.iterdir()
                    if d.is_dir() and re.fullmatch(r"[0-9.eE+-]+", d.name)),
                   key=lambda d: float(d.name))
    gas = [s for s in sp if (times[0] / s).exists()]
    enthalpy, rxns = reactions(case, set(sol), set(gas))
    print(f"mode: {'enthalpy' if enthalpy else 'hr (hr = 0)'}; reactions: "
          + "; ".join(eq for eq, _, _ in rxns))
    bad = []

    def expect(what, ok):
        if not ok:
            bad.append(what)

    for old, new in zip(times, times[1:]):
        dt = float(new.name) - float(old.name)
        Yo = {s: internal(old / s) for s in gas}
        Yn = {s: internal(new / s) for s in gas}
        To, Tn = internal(old / "T"), internal(new / "T")
        Tso, Tsn = internal(old / "Ts"), internal(new / "Ts")
        po, eps = internal(old / "p"), internal(old / "porosityF")
        Ymo = solid_mass(old, sol)

        r = [(Ymo[s] - internal(new / f"Y{s}m")) / dt for _, s, _ in rxns]
        S = {i: sum(rk * nu.get(i, 0.0) for rk, (_, _, nu) in zip(r, rxns)) for i in gas}
        RRs = {j: sum(rk * nu.get(j, 0.0) for rk, (_, _, nu) in zip(r, rxns)) for j in sol}
        Srho = sum(S.values())
        made = {i: sum(rk * max(nu.get(i, 0.0), 0) for rk, (_, _, nu) in zip(r, rxns)) for i in gas}
        eaten = {i: made[i] - S[i] for i in gas}

        Qp = sum(S[i] * hs(sp[i], Tso) for i in gas if S[i] > 0)
        Qr = sum(S[i] * hs(sp[i], To) for i in gas if S[i] < 0)
        Qg = Qp + Qr
        Qa = sum(-S[i] * (hs(sp[i], Tso) - hs(sp[i], To)) for i in gas if S[i] < 0)
        Sch = 0.0
        if enthalpy:
            Sch = (-sum(RRs[j] * (sol[j][0] * (Tso - TSTD) + sol[j][1]) for j in sol)
                   - sum(S[i] * ha(sp[i], Tso) for i in gas))

        # Gas: mixing at constant mass-weighted hs (outflow cancels, Euler).
        Wmix = 1 / sum(Yo[s] / sp[s][0] for s in gas)
        m = eps * po * Wmix / (RR * To)
        M = m + dt * Srho
        Yx = {s: (m * Yo[s] + dt * S[s]) / M for s in gas}
        hold = sum(Yo[s] * hs(sp[s], To) for s in gas)
        Tx = solve_T(sp, Yx, (m * hold + dt * Qg) / M, To)
        dEg = M * sum(Yn[s] * hs(sp[s], Tn) for s in gas) - m * hold

        # Solid: carrier capacity on the pre-reaction (= step-start) mass.
        rhoCp = sum(Ymo[j] * sol[j][0] for j in sol)
        Tsx = Tso + dt * (Sch - Qa) / rhoCp
        dEs = rhoCp * (Tsn - Tso)

        scale = dt * (abs(Qg) + abs(Qa) + abs(Sch))
        tot = dEg + dEs - dt * (Qg - Qa + Sch)
        print(f"t={new.name:>8}  r={' '.join(f'{x:.6e}' for x in r)}")
        for i in gas:
            if made[i] > 0 and eaten[i] > 0:
                shift = min(made[i], eaten[i]) * (hs(sp[i], Tso) - hs(sp[i], To))
                print(f"   {i}: made {made[i]:.6e}, consumed {eaten[i]:.6e}, net {S[i]:+.6e} kg/m3/s;"
                      f" netting moves {shift:+.6e} W/m3 off the solid's heat-up and off the"
                      f" gas's gain, against gross bookkeeping")
        if a.both_ways:
            expect(f"{new.name}: {a.both_ways} both made and consumed",
                   made[a.both_ways] > 0 and eaten[a.both_ways] > 0)
        print(f"   gas   T={Tn:.9f}  closed form {Tx:.9f}  diff {Tn - Tx:+.3e} K   "
              f"dE={dEg:+.9e}  dt*Q_int,g={dt * Qg:+.9e}")
        print(f"   solid Ts={Tsn:.9f}  closed form {Tsx:.9f}  diff {Tsn - Tsx:+.3e} K   "
              f"dE={dEs:+.9e}  dt*(S_chem-Q_heat)={dt * (Sch - Qa):+.9e}")
        print(f"   total dE_gas+dE_solid - dt*(sum S_i hs_i(Ts) + S_chem) = {tot:+.3e} J/m3 "
              f"(of {scale:.3e} exchanged)")
        expect(f"{new.name}: gas T", abs(Tn - Tx) <= a.tol)
        expect(f"{new.name}: solid Ts", abs(Tsn - Tsx) <= a.tol)
        expect(f"{new.name}: Y", all(abs(Yn[s] - Yx[s]) <= YTOL for s in gas))
        expect(f"{new.name}: total", abs(tot) <= ETOL * scale)
        w = {f: internal(new / f) for f in ("Srho", "gasProductEnthalpyAtTs",
             "gasReactantEnthalpyAtTg", "heatUpGasReactantsToTs")}
        w["gasProductEnthalpyAtTs + gasReactantEnthalpyAtTg"] = (
            w["gasProductEnthalpyAtTs"] + w["gasReactantEnthalpyAtTg"])
        fields = [("Srho", Srho, Srho), ("gasProductEnthalpyAtTs", Qp, Qp),
                  ("gasReactantEnthalpyAtTg", Qr, Qr),
                  ("gasProductEnthalpyAtTs + gasReactantEnthalpyAtTg", Qg, Qg),
                  ("heatUpGasReactantsToTs", Qa, Qg)]
        if enthalpy:
            w["solidChemistrySh"] = internal(new / "solidChemistrySh")
            fields.append(("solidChemistrySh", Sch, Sch))
        for f, x, sc in fields:
            print(f"   {f:48s} written {w[f]:+.9e}  hand {x:+.9e}")
            expect(f"{new.name}: {f}", rel(w[f], x, sc) <= FTOL or abs(w[f] - x) <= 1e-12)
        if enthalpy:
            dAbs = (dEg + dt * sum(S[i] * ha(sp[i], TSTD) for i in gas)
                    + sum((Ymo[j] + dt * RRs[j]) * (sol[j][0] * (Tsn - TSTD) + sol[j][1])
                          - Ymo[j] * (sol[j][0] * (Tso - TSTD) + sol[j][1]) for j in sol))
            lag = dt * sum(RRs[j] * sol[j][0] for j in sol) * (Tsn - Tso)
            print(f"   absolute enthalpy change {dAbs:+.6e} J/m3, converted-solid lag "
                  f"dt*sum RRs Cp dTs {lag:+.6e}, difference {dAbs - lag:+.3e}")
            expect(f"{new.name}: absolute enthalpy", abs(dAbs - lag) <= ETOL * scale)

    print("FAIL: " + ", ".join(bad) if bad else "PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
