#!/usr/bin/env python3
"""Single-cell heterogeneous rate fixtures (#209 block A, shapes from #201).

``setup BASE --reaction EQ --coeffs C [--stoichiometric] [--diffusion]
[--dt DT] [--set FIELD=VALUE ...]`` copies ../BASE into the calling case and
reduces it to one cell and one step of DT, with EQ as the only solid reaction.
The species sources are the ODE's change over the step divided by DT, so DT is
a compromise: too short and the change cancels against the mass it is taken
from, too long and the rate drifts within the step.

``check`` reads the first step's species sources from log.porousGasificationFoam
and the heat source from postProcessing, recomputes both by hand from the
case's own input files (Eq. 4, 5 and 10 of Zuk et al. 2022, then the species
split and Eq. 12), and writes the measured/hand ratios to
postProcessing/regressionSolidRates/0/solidRates.dat, where
system/regressionInvariants holds them at 1.

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


def split_coefficients(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W):
    """Signed source per unit Omega for each species, as the solver splits it.

    Non-stoichiometric: every coefficient over Sigma = solid + gas LHS
    coefficients (#201: the paper's Eq. 6 applies them undivided).
    Stoichiometric: solids per kg of solid substrate; the net solid loss is
    shared among the gases in proportion to W*nu (paper Sec. 4.2.3).
    """
    split: dict[str, float] = {}

    def add(name: str, value: float) -> None:
        split[name] = split.get(name, 0.0) + value

    if not stoich:
        total = sum(nu for nu, _ in lhs_s) + sum(nu for nu, _ in lhs_g)
        for sign, side in ((-1, lhs_s), (-1, lhs_g), (1, rhs_s), (1, rhs_g)):
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
    for sign, side in ((-1, lhs_g), (1, rhs_g)):
        for nu, name in side:
            add(name, sign * released * W[name] * nu / dW)
    return split


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
    if diffusion and lhs_g and r > 0:
        # Eq. 10: harmonic sum with the specie transfer of each gas reactant.
        st = case / "constant/specieTransferProperties"
        st_text = st.read_text()
        if entry(st_text, "specieTransferModel", st) != "constST":
            fail(f"{st}: only constST is supported")
        ST = float(entry(st_text, "h", st)) * float(entry(st_text, "SAV", st))
        p, Tg = uniform(case, "p"), uniform(case, "T")
        thermo = (case / "constant/thermo.compressibleGas").read_text()
        allGas = [f.name for f in (case / "0.orig").iterdir()
                  if re.search(rf"^{re.escape(f.name)}\s*$", thermo, re.M)]
        Wmix = 1 / sum(uniform(case, g) / gas_weight(case, g) for g in allGas)
        rho_g = p * Wmix / (RR * Tg)
        r = 1 / (1 / r + sum(1 / (ST * uniform(case, n) * rho_g) for _, n in lhs_g))

    if r == 0:
        fail("the hand rate is zero: the fixture does not react")
    solid_fraction = 1 - eps
    split = split_coefficients(lhs_s, lhs_g, rhs_s, rhs_g, stoich, W)
    hand = {n: solid_fraction * r * c for n, c in split.items()}
    heat_hand = -solid_fraction * r * hr  # Eq. 12, not split

    got = first_step_sources(case / "log.porousGasificationFoam")
    heat = postprocessed(case, "solidRatesHeat", "volAverage(solidChemistrySh)")

    names = sorted(hand)
    for n in names:
        if n not in got:
            fail(f"no first-step source for {n} in the log")
    stray = {n: v for n, v in got.items() if n not in hand and v != 0}
    if stray:
        fail(f"sources for species outside the reaction: {stray}")
    ratios = [got[n] / hand[n] for n in names] + [heat / heat_hand]
    produced = sum(v for v in got.values() if v > 0)
    consumed = -sum(v for v in got.values() if v < 0)

    out = case / "postProcessing" / FUNCTION / "0" / "solidRates.dat"
    out.parent.mkdir(parents=True, exist_ok=True)
    cols = [f"source({n})" for n in names] + ["heat", "rateRatioMin", "rateRatioMax", "massRatio"]
    vals = [got[n] for n in names] + [heat, min(ratios), max(ratios), produced / consumed]
    out.write_text(
        f"# {eq} ({' '.join(f'{c:g}' for c in coeffs)}) stoichiometric {stoich} diffusion {diffusion}\n"
        f"# hand: Omega {r:.10e} " + " ".join(f"{n} {hand[n]:.10e}" for n in names)
        + f" heat {heat_hand:.10e}\n"
        "# Time " + " ".join(cols) + "\n"
        f"{dt:g} " + " ".join(f"{v:.10e}" for v in vals) + "\n")
    for n in names:
        print(f"{n:8s} code {got[n]: .10e}  hand {hand[n]: .10e}  ratio {got[n] / hand[n]:.12f}")
    print(f"heat     code {heat: .10e}  hand {heat_hand: .10e}  ratio {heat / heat_hand:.12f}")
    print(f"mass     produced/consumed {produced / consumed:.12f}")


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
    s.add_argument("--set", nargs="*", default=[], metavar="FIELD=VALUE",
                   help="uniform internalField of a 0.orig field")
    sp.add_parser("check")
    args = ap.parse_args()
    setup(args) if args.cmd == "setup" else check(args)


if __name__ == "__main__":
    main()
