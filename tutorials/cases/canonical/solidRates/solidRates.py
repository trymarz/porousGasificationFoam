#!/usr/bin/env python3
"""Single-cell heterogeneous rate fixtures (#209 block A, shapes from #201).

``setup BASE --reaction EQ --coeffs C [--stoichiometric] [--diffusion]
[--dt DT] [--agent-limit TOL] [--set FIELD=VALUE ...]`` copies ../BASE into
the calling case and reduces it to one cell and one step of DT, with EQ as the
only solid reaction.
The species sources are the ODE's change over the step divided by DT, so DT is
a compromise: too short and the change cancels against the mass it is taken
from, too long and the rate's fall within the step stops being one exponential.

``check`` reads the first step's species sources from log.porousGasificationFoam
and the heat source from postProcessing, recomputes both by hand from the
case's own input files (Eq. 4, 5 and 10 of Zuk et al. 2022, then the species
split and Eq. 12), and writes the measured/hand ratios to
postProcessing/regressionSolidRates/0/solidRates.dat, where
system/regressionInvariants holds them at 1. The sources are means over the
step, so each hand value is its step-start value times step_mean(a), with a the
fall of the rate over the step, from the same hand quantities (#200).
``check --agent-limit`` also
writes agentRatio, the gas reactant's consumption over its Eq. 9 delivery,
which is 1 at the diffusion limit; ``setup --agent-limit TOL`` declares it.
``setup --enthalpy HF`` sets solidReactionEnergyFromEnthalpy true and the
solid's Hf; check then integrates the coupled c(t), T(c) by hand (#209 H).

The species split is the code's current convention (split_coefficients): a
change to it in the solver has to change that function too.
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import sys
from pathlib import Path

# The solver's universal gas constant, k/mu from OpenFOAM v2406 etc/controlDict.
RR = 1.38065e-23 / 1.66054e-27  # J/(kmol K)
FUNCTION = "regressionSolidRates"


def fail(msg: str) -> None:
    print(f"solidRates: {msg}", file=sys.stderr)
    sys.exit(2)


def sub(text: str, pattern: str, repl: str, path: Path, flags: int = re.M) -> str:
    new, n = re.subn(pattern, repl, text, count=1, flags=flags)
    if n != 1:
        fail(f"{path}: no match for {pattern!r}")
    return new


def edit(path: Path, *edits: tuple[str, str]) -> None:
    text = path.read_text()
    for pattern, repl in edits:
        text = sub(text, pattern, repl, path)
    path.write_text(text)


def setup(args: argparse.Namespace) -> None:
    case = Path.cwd()
    base = Path(__file__).resolve().parent.parent / args.base
    for d in ("0.orig", "constant", "system"):
        shutil.copytree(base / d, case / d, symlinks=True)
    # The thermoData #include is relative to the base's constant/, one level up.
    sp = case / "constant/solidThermophysicalProperties"
    sp.write_text(sp.read_text().replace('#include "../', '#include "../../'))

    stoich = "true" if args.stoichiometric else "false"
    diffusion = "true" if args.diffusion else "false"
    cp = case / "constant/chemistryProperties"
    text = cp.read_text()
    text = sub(text, r"^solidReactions\s*\(.*?^\);",
               "solidReactions\n(\n    irreversibleSolidArrheniusHeterogeneousReaction\n"
               f"    {args.reaction}\n    ({args.coeffs})\n);", cp, re.M | re.S)
    text = sub(text, r"^stoichiometricReactions\s+\w+;",
               f"stoichiometricReactions {stoich};", cp)
    if re.search(r"^diffusionLimitedReactions\s", text, re.M):
        text = sub(text, r"^diffusionLimitedReactions\s+\w+;",
                   f"diffusionLimitedReactions {diffusion};", cp)
    else:
        text = sub(text, r"^stoichiometricReactions",
                   f"diffusionLimitedReactions {diffusion};\nstoichiometricReactions", cp)
    cp.write_text(text)

    edit(case / "system/blockMeshDict",
         (r"\(\s*\d+\s+\d+\s+\d+\s*\)\s*simpleGrading", "(1 1 1) simpleGrading"))
    edit(case / "system/controlDict",
         (r"^deltaT\s+[^;]*;", f"deltaT {args.dt:g};"),
         (r"^endTime\s+[^;]*;", f"endTime {args.dt:g};"),
         (r"^writeControl\s+[^;]*;", "writeControl timeStep;"),
         (r"^writeInterval\s+[^;]*;", "writeInterval 1;"),
         (r"^writePrecision\s+[^;]*;", "writePrecision 12;"),
         (r"^adjustTimeStep\s+[^;]*;", "adjustTimeStep no;"))
    shutil.copy(Path(__file__).resolve().parent / "regressionFunctions",
                case / "system/regressionFunctions")
    shutil.copy(Path(__file__).resolve().parent / "regressionInvariants",
                case / "system/regressionInvariants")
    if args.enthalpy is not None:
        # The hand value is the continuous solution; the solver's Ti advances
        # per sub-step, which leaves ~1e-3 (case-specific, see README).
        text = cp.read_text()
        cp.write_text(sub(text, r"^solidReactionEnergyFromEnthalpy\s+\w+;",
                          "solidReactionEnergyFromEnthalpy true;", cp))
        edit(sp, (r"\bHf\s+[^;]*;", f"Hf      {args.enthalpy:g};"))
        edit(case / "system/regressionInvariants",
             *((rf"(rateRatio{m}\s+==\s+1\s+)1e-5", rf"\g<1>{args.enthalpy_tol:g}")
               for m in ("Min", "Max")))
    if args.agent_limit is not None:
        with open(case / "system/regressionInvariants", "a") as f:
            f.write(f"regressionSolidRates  agentRatio    ==  1  {args.agent_limit:g}\n")

    for item in args.set:
        name, value = item.split("=")
        edit(case / "0.orig" / name,
             (r"internalField\s+uniform\s+[^;]*;", f"internalField   uniform {value};"))


def uniform(case: Path, field: str) -> float:
    path = case / "0.orig" / field
    m = re.search(r"^internalField\s+uniform\s+([^;]+);", path.read_text(), re.M)
    if not m:
        fail(f"{path}: internalField is not uniform")
    return float(m.group(1))


def entry(text: str, key: str, path: Path) -> str:
    m = re.search(rf"^\s*{key}\s+([^;]+);", text, re.M)
    if not m:
        fail(f"{path}: no {key}")
    return m.group(1).strip()


def block(text: str, name: str, path: Path) -> str:
    """Body of the top-level 'name { ... }' dictionary (braces balanced)."""
    m = re.search(rf"^{re.escape(name)}\s*\{{", text, re.M)
    if not m:
        fail(f"{path}: no {name} dictionary")
    depth, i = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def solid_density(case: Path, name: str) -> float:
    path = case / "constant/solidThermophysicalProperties"
    text = path.read_text()
    body = block(text, f"{name}Coeffs", path)
    ref = re.fullmatch(r"\s*\$(\w+);\s*", body)
    if ref:
        data = (path.parent / re.search(r'#include\s+"([^"]+)"', text).group(1)).resolve()
        body = block(data.read_text(), ref.group(1), data)
    return float(re.search(r"\brho\s+([-+0-9.eE]+);", body).group(1))


def gas_weight(case: Path, name: str) -> float:
    path = case / "constant/thermo.compressibleGas"
    return float(entry(block(path.read_text(), name, path), "molWeight", path))


def parse_reaction(eq: str) -> tuple[list[tuple[float, str]], list[tuple[float, str]]]:
    def side(s: str) -> list[tuple[float, str]]:
        terms = []
        for t in s.split("+"):
            parts = t.split()
            terms.append((float(parts[0]), parts[1]) if len(parts) == 2 else (1.0, parts[0]))
        return terms
    lhs, rhs = eq.split("=")
    return side(lhs), side(rhs)


def agent_factors(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W):
    """kg of each gas reactant consumed per unit Omega, as the solver splits it.

    Also each agent's Eq. 10 factor: its resistance is xi_a/Omega_diff (#201).
    """
    if not stoich:
        total = sum(nu for nu, _ in lhs_s) or sum(nu for nu, _ in lhs_g)
        return {name: nu / total for nu, name in lhs_g}
    solid = sum(nu for nu, _ in lhs_s)
    released = 1 - sum(nu for nu, _ in rhs_s) / solid
    dW = sum(W[n] * nu for nu, n in rhs_g) - sum(W[n] * nu for nu, n in lhs_g)
    return {name: released * W[name] * nu / dW for nu, name in lhs_g}


def split_coefficients(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W):
    """Signed source per unit Omega for each species, as the solver splits it.

    Non-stoichiometric: Eq. 6 with the rate per kg of solid substrate, every
    coefficient over the solid LHS coefficients' sum (#201).
    Stoichiometric: solids per kg of solid substrate; the net solid loss is
    shared among the gases in proportion to W*nu (paper Sec. 4.2.3).
    """
    split: dict[str, float] = {}

    def add(name: str, value: float) -> None:
        split[name] = split.get(name, 0.0) + value

    for name, xi in agent_factors(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W).items():
        add(name, -xi)
    if not stoich:
        total = sum(nu for nu, _ in lhs_s) or sum(nu for nu, _ in lhs_g)
        for sign, side in ((-1, lhs_s), (1, rhs_s), (1, rhs_g)):
            for nu, name in side:
                add(name, sign * nu / total)
        return split
    solid = sum(nu for nu, _ in lhs_s)
    for nu, name in lhs_s:
        add(name, -nu / solid)
    for nu, name in rhs_s:
        add(name, nu / solid)
    released = 1 - sum(nu for nu, _ in rhs_s) / solid
    dW = sum(W[n] * nu for nu, n in rhs_g) - sum(W[n] * nu for nu, n in lhs_g)
    for nu, name in rhs_g:
        add(name, released * W[name] * nu / dW)
    return split


def gas_density(case: Path) -> float:
    """Gas density of the initial mixture from the ideal gas law [kg/m3]."""
    p, Tg = uniform(case, "p"), uniform(case, "T")
    thermo = (case / "constant/thermo.compressibleGas").read_text()
    allGas = [f.name for f in (case / "0.orig").iterdir()
              if re.search(rf"^{re.escape(f.name)}\s*$", thermo, re.M)]
    Wmix = 1 / sum(uniform(case, g) / gas_weight(case, g) for g in allGas)
    return p * Wmix / (RR * Tg)


def step_mean(a: float) -> float:
    """Mean over the step of a rate falling as exp(-a t/dt), over its start value."""
    return -math.expm1(-a) / a if a else 1.0


def enthalpy_step(k, c0: float, T0: float, hf: float, Cp: float, dt: float) -> float:
    """Solid concentration after dt of dc/dt = -k(T) c with T = T0 + hf/Cp ln(c0/c).

    One solid going to gas: dT = -hf dc/(c Cp), the solver's enthalpy-mode heating.
    """
    def f(c: float) -> float:
        return -k(T0 + hf / Cp * math.log(c0 / c)) * c
    n = 20000
    h, c = dt / n, c0
    for _ in range(n):  # RK4; n steps resolve it to roundoff
        k1 = f(c); k2 = f(c + h / 2 * k1); k3 = f(c + h / 2 * k2); k4 = f(c + h * k3)
        c += h / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
    return c


def first_step_sources(log: Path) -> dict[str, float]:
    text = log.read_text(errors="replace")
    m = re.search(r"^Time = .*?(?=^Time = |\Z)", text, re.M | re.S)
    if not m:
        fail(f"{log}: no time step")
    step = m.group(0)
    out: dict[str, float] = {}
    for name, lo, hi in re.findall(
            r"^solid Y(\w+) equation solved\. Sources min/max\s*=\s*(\S+), (\S+);", step, re.M):
        out[name] = float(lo)
    for name, lo, hi in re.findall(
            r"^ gas\s+(\w+) Srho min/max\s*=\s*(\S+), (\S+) ", step, re.M):
        out[name] = float(lo)
    if not out:
        fail(f"{log}: no solid or gas source lines in the first step")
    return out


def postprocessed(case: Path, func: str, column: str) -> float:
    files = sorted((case / "postProcessing" / func).glob("*/*.dat"))
    if not files:
        fail(f"no postProcessing/{func}")
    lines = [l for l in files[0].read_text().splitlines() if l.strip()]
    header = [l for l in lines if l.startswith("#")][-1].lstrip("#").split()
    rows = [l.split() for l in lines if not l.startswith("#")]
    row = next(r for r in rows if float(r[0]) > 0)
    return float(row[header.index(column)])


def check(args: argparse.Namespace) -> None:
    case = Path.cwd()
    cp = case / "constant/chemistryProperties"
    text = cp.read_text()
    m = re.search(r"^solidReactions\s*\(\s*\w+\s*\n\s*([^\n]+?)\s*\n\s*\(([^)]*)\)", text, re.M)
    if not m:
        fail(f"{cp}: cannot read the single solid reaction")
    eq, coeffs = m.group(1), [float(x) for x in m.group(2).split()]
    stoich = entry(text, "stoichiometricReactions", cp) in ("true", "on", "yes")
    diffusion = entry(text, "diffusionLimitedReactions", cp) in ("true", "on", "yes")
    sp = case / "constant/solidThermophysicalProperties"
    solids = entry(sp.read_text(), "solidComponents", sp).strip("() ").split()

    cd = case / "system/controlDict"
    dt = float(entry(cd.read_text(), "deltaT", cd))
    lhs, rhs = parse_reaction(eq)
    lhs_s = [t for t in lhs if t[1] in solids]
    lhs_g = [t for t in lhs if t[1] not in solids]
    rhs_s = [t for t in rhs if t[1] in solids]
    rhs_g = [t for t in rhs if t[1] not in solids]
    gases = [n for _, n in lhs_g + rhs_g]
    W = {n: gas_weight(case, n) for n in gases}

    A, Ta, Tcrit, hr = coeffs[:4]
    orders = coeffs[4:]
    T = uniform(case, "Ts")
    eps = uniform(case, "porosityF")
    Ys = {n: uniform(case, f"Y{n}") for n in solids}
    rho_s = 1 / sum(Ys[n] / solid_density(case, n) for n in solids)

    # Eq. 4-5: kappa per kg of solid substrate, zero below Tcrit.
    r = A * math.exp(-Ta / T) if T >= Tcrit else 0.0
    for (_, n), order in zip(lhs_s + lhs_g, orders):
        r *= (Ys[n] if n in solids else uniform(case, n)) ** order
    r *= rho_s
    kinetic = r
    xi = agent_factors(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W)
    rho_g = gas_density(case) if lhs_g else 0.0
    delivered = {}
    if diffusion and lhs_g and r > 0:
        # Eq. 10: harmonic sum with the specie transfer of each gas reactant.
        st = case / "constant/specieTransferProperties"
        st_text = st.read_text()
        if entry(st_text, "specieTransferModel", st) != "constST":
            fail(f"{st}: only constST is supported")
        ST = float(entry(st_text, "h", st)) * float(entry(st_text, "SAV", st))
        # Eq. 9 delivery; #201: each agent's resistance is xi_a/Omega_diff.
        delivered = {n: ST * uniform(case, n) * rho_g for _, n in lhs_g}
        r = 1 / (1 / r + sum(xi[n] / delivered[n] for _, n in lhs_g))

    if r == 0:
        fail("the hand rate is zero: the fixture does not react")
    solid_fraction = 1 - eps
    split = split_coefficients(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W)
    hand = {n: solid_fraction * r * c for n, c in split.items()}

    # a = sum_k (d ln r/d ln c_k)(-dc_k/dt) dt/c_k over the reactants, at the step
    # start. Under Eq. 10 a kinetic order counts by r/kinetic, and a gas reactant
    # gains its delivery's share of the resistance, r*xi/delivered.
    a = 0.0
    for (_, n), order in zip(lhs_s + lhs_g, orders):
        solid = n in solids
        c0 = solid_fraction * rho_s * Ys[n] if solid else eps * rho_g * uniform(case, n)
        weight = order * r / kinetic
        if n in delivered:
            weight += r * xi[n] / delivered[n]
        if weight:
            a -= weight * hand[n] / c0 * dt
    mean = step_mean(a)
    hand = {n: v * mean for n, v in hand.items()}
    heat_hand = -solid_fraction * r * hr * mean  # Eq. 12, not split

    enthalpy = entry(text, "solidReactionEnergyFromEnthalpy", cp) in ("true", "on", "yes")
    if enthalpy:
        # Sh then has no hand form here, so only the species sources are held.
        if len(solids) != 1 or lhs_g or rhs_s or orders != [1.0]:
            fail("enthalpy mode supports one solid, first order, to gas only")
        n0 = solids[0]
        c0 = solid_fraction * rho_s * Ys[n0]
        hf, Cp = (float(entry(block(sp.read_text(), f"{n0}Coeffs", sp), k, sp))
                  for k in ("Hf", "Cp"))
        cEnd = enthalpy_step(lambda Tk: A * math.exp(-Ta / Tk) if Tk >= Tcrit else 0.0,
                             c0, T, hf, Cp, dt)
        r = (c0 - cEnd) / dt / solid_fraction
        # derivatives() caps the ODE's |dT/dt| at 500 K/s; the hand value has no cap.
        if abs(hf / Cp * A * math.exp(-Ta / (T + hf / Cp * math.log(c0 / cEnd)))) >= 500:
            fail("enthalpy heating reaches the solver's 500 K/s cap")
        hand = {n: solid_fraction * r * c for n, c in split.items()}
        mean = 1.0

    got = first_step_sources(case / "log.porousGasificationFoam")
    heat = postprocessed(case, "solidRatesHeat", "volAverage(solidChemistrySh)")

    names = sorted(hand)
    for n in names:
        if n not in got:
            fail(f"no first-step source for {n} in the log")
    stray = {n: v for n, v in got.items() if n not in hand and v != 0}
    if stray:
        fail(f"sources for species outside the reaction: {stray}")
    ratios = [got[n] / hand[n] for n in names] + ([] if enthalpy else [heat / heat_hand])
    produced = sum(v for v in got.values() if v > 0)
    consumed = -sum(v for v in got.values() if v < 0)

    cols = [f"source({n})" for n in names] + ["heat", "rateRatioMin", "rateRatioMax", "massRatio"]
    vals = [got[n] for n in names] + [heat, min(ratios), max(ratios), produced / consumed]
    if args.agent_limit:
        # The agent's consumption over its Eq. 9 delivery; 1 at the diffusion limit.
        if len(lhs_g) != 1 or not delivered:
            fail("--agent-limit needs one gas reactant and diffusionLimitedReactions")
        agent = lhs_g[0][1]
        cols.append("agentRatio")
        vals.append(-got[agent] / (solid_fraction * delivered[agent]))

    out = case / "postProcessing" / FUNCTION / "0" / "solidRates.dat"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        f"# {eq} ({' '.join(f'{c:g}' for c in coeffs)}) stoichiometric {stoich} diffusion {diffusion}\n"
        f"# hand: Omega {r:.10e} " + " ".join(f"{n} {hand[n]:.10e}" for n in names)
        + f" heat {heat_hand:.10e} stepMean {mean:.12f}\n"
        "# Time " + " ".join(cols) + "\n"
        f"{dt:g} " + " ".join(f"{v:.10e}" for v in vals) + "\n")
    for n in names:
        print(f"{n:8s} code {got[n]: .10e}  hand {hand[n]: .10e}  ratio {got[n] / hand[n]:.12f}")
    if not enthalpy:
        print(f"heat     code {heat: .10e}  hand {heat_hand: .10e}  ratio {heat / heat_hand:.12f}")
    print(f"mass     produced/consumed {produced / consumed:.12f}")
    if args.agent_limit:
        print(f"agent    consumed/delivered {vals[-1]:.12f}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("setup")
    s.add_argument("base")
    s.add_argument("--reaction", required=True)
    s.add_argument("--coeffs", required=True)
    s.add_argument("--stoichiometric", action="store_true")
    s.add_argument("--diffusion", action="store_true")
    s.add_argument("--dt", type=float, default=1e-8)
    s.add_argument("--agent-limit", type=float, metavar="TOL",
                   help="also hold agentRatio (check --agent-limit) at 1 within TOL")
    s.add_argument("--enthalpy", type=float, metavar="HF",
                   help="solidReactionEnergyFromEnthalpy true, with this solid Hf [J/kg]")
    s.add_argument("--enthalpy-tol", type=float, default=5e-3, metavar="TOL",
                   help="rateRatio tolerance with --enthalpy")
    s.add_argument("--set", nargs="*", default=[], metavar="FIELD=VALUE",
                   help="uniform internalField of a 0.orig field")
    c = sp.add_parser("check")
    c.add_argument("--agent-limit", action="store_true",
                   help="also write agentRatio, the gas reactant's consumption over its delivery")
    args = ap.parse_args()
    setup(args) if args.cmd == "setup" else check(args)


if __name__ == "__main__":
    main()
