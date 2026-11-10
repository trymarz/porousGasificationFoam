#!/usr/bin/env python3
"""Gas reaction in a sealed porous cell: species against Qdot*eps (#226).

Reads the state after each step from postProcessing/regressionGasState and,
for every step, writes to postProcessing/regressionGasReactionPorosity/0/:

- energyRatio = -sum_i(Hc_i dY_i)/dh. EEqn books eps*Qdot = -eps*sum(Hc_i RR_i)
  and YEqn eps*RR_i, both over d(eps*rho*.)/dt, so the ratio is 1 when the two
  carry the same eps weight and 1/eps when the species side drops it.
- speciesRatio = dY_i over its hand value RR_i*dt/rho, with RR_i = nu_i W_i
  omega and omega = A T^beta exp(-Ta/T) [CO]^2 [O2] at the step start, taken
  to its step mean to first order (concentrations and the ODE's own heating).
- massChange = sum_i dY_i, zero for a gas reaction.

The step-0 state is the case's 0.orig; h there is sum_i Y_i hs_i(T) by hand.
"""

from __future__ import annotations

import math
import re
import sys
from pathlib import Path

# The solver's RR = 1e3*NA*k (v2406 thermodynamicConstants.C; NA from
# fundamentalConstants.C, k from etc/controlDict). k/mu is 7.7e-7 off it.
RR = 1e3 * 6.0221417930e23 * 1.38065e-23  # J/(kmol K)
TSTD = 298.15
SPECIES = ("N2", "O2", "CO", "CO2")
NU = {"CO": -2.0, "O2": -1.0, "CO2": 2.0}  # 2 CO + O2 = 2 CO2, orders = nu
FUNCTION = "regressionGasReactionPorosity"


def fail(msg: str) -> None:
    print(f"gasReactionPorosity: {msg}", file=sys.stderr)
    sys.exit(2)


def entry(text: str, key: str, path: Path) -> str:
    m = re.search(rf"^\s*{key}\s+([^;]+);", text, re.M)
    if not m:
        fail(f"{path}: no {key}")
    return m.group(1).strip()


def block(text: str, name: str) -> str:
    m = re.search(rf"^\s*{re.escape(name)}\s*\{{", text, re.M)
    if not m:
        fail(f"no {name} dictionary")
    depth, i = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def uniform(case: Path, field: str) -> float:
    path = case / "0.orig" / field
    return float(entry(path.read_text(), "internalField", path).split()[1])


class Janaf:
    """OpenFOAM janafThermo per kg: coefficients times R/W."""

    def __init__(self, text: str, name: str):
        body = block(text, name)
        self.W = float(re.search(r"molWeight\s+([^;]+);", body).group(1))
        self.Tc = float(re.search(r"Tcommon\s+([^;]+);", body).group(1))
        coeffs = lambda key: [float(x) for x in re.search(
            rf"{key}\s*\(([^)]*)\)", body).group(1).split()]
        self.hi, self.lo = coeffs("highCpCoeffs"), coeffs("lowCpCoeffs")
        self.R = RR / self.W

    def a(self, T: float) -> list[float]:
        return self.lo if T < self.Tc else self.hi

    def Cp(self, T: float) -> float:
        a = self.a(T)
        return self.R * ((((a[4] * T + a[3]) * T + a[2]) * T + a[1]) * T + a[0])

    def Ha(self, T: float) -> float:
        a = self.a(T)
        return self.R * (((((a[4] / 5 * T + a[3] / 4) * T + a[2] / 3) * T
                          + a[1] / 2) * T + a[0]) * T + a[5])

    def Hc(self) -> float:
        return self.Ha(TSTD)


def states(case: Path) -> list[dict[str, float]]:
    files = sorted((case / "postProcessing/regressionGasState").glob("*/volFieldValue.dat"))
    if not files:
        fail("no postProcessing/regressionGasState")
    lines = [l for l in files[0].read_text().splitlines() if l.strip()]
    header = [l for l in lines if l.startswith("#")][-1].lstrip("#").split()
    out = []
    for l in lines:
        if l.startswith("#"):
            continue
        row = dict(zip(header, map(float, l.split())))
        out.append({"t": row["Time"], **{
            k[len("volAverage("):-1]: v for k, v in row.items() if k.startswith("volAverage(")}})
    return out


def main() -> None:
    case = Path.cwd()
    tp = case / "constant/thermo.compressibleGas"
    thermo = {n: Janaf(tp.read_text(), n) for n in SPECIES}
    rp = case / "constant/reactions"
    rtext = block(rp.read_text(), "OxidationOfCarbonMonoxide")
    if entry(rtext, "reaction", rp).strip('"') != "2 CO + O2 = 2 CO2":
        fail(f"{rp}: the checker knows only 2 CO + O2 = 2 CO2")
    A, beta, Ta = (float(entry(rtext, k, rp)) for k in ("A", "beta", "Ta"))
    cd = case / "system/controlDict"
    dt = float(entry(cd.read_text(), "deltaT", cd))
    eps = uniform(case, "porosityF")

    Y0 = {n: uniform(case, n) for n in SPECIES}
    T0 = uniform(case, "T")
    h0 = sum(Y0[n] * (thermo[n].Ha(T0) - thermo[n].Hc()) for n in SPECIES)
    run = states(case)
    if len(run) < 1:
        fail("no step in regressionGasState")
    rho = run[0]["rho"]
    rho_hand = uniform(case, "p") / (RR * T0 * sum(Y0[n] / thermo[n].W for n in SPECIES))
    prev = {"t": 0.0, "T": T0, "h": h0, "rho": rho_hand, **Y0}

    rows = []
    for cur in run:
        dY = {n: cur[n] - prev[n] for n in SPECIES}
        dh = cur["h"] - prev["h"]
        energy = -sum(thermo[n].Hc() * dY[n] for n in SPECIES) / dh

        T, r = prev["T"], prev["rho"]
        c = {n: r * prev[n] / thermo[n].W for n in NU}
        omega = A * T**beta * math.exp(-Ta / T) * c["CO"] ** 2 * c["O2"]
        RRi = {n: NU[n] * thermo[n].W * omega for n in NU}
        # First-order step mean: d ln(omega)/dt from the concentrations and
        # the ODE's heating dT/dt = -sum(Ha_i RR_i)/(rho Cp) at the step start.
        cp = sum(prev[n] * thermo[n].Cp(T) for n in SPECIES)
        dTdt = -sum(thermo[n].Ha(T) * RRi[n] for n in NU) / (r * cp)
        dlnw = sum(-NU[n] * NU[n] * omega / c[n] for n in ("CO", "O2")) \
            + (beta / T + Ta / T**2) * dTdt
        mean = 1 + 0.5 * dlnw * dt
        species = {n: dY[n] / (RRi[n] * dt * mean / r) for n in NU}
        rows.append((cur["t"], dY, dh, energy, species, sum(dY.values())))
        print(f"t = {cur['t']:g}: dh {dh:.10e}  -sum(Hc dY) {-sum(thermo[n].Hc() * dY[n] for n in SPECIES):.10e}"
              f"  energyRatio {energy:.12f}")
        for n in NU:
            print(f"  {n:4s} dY {dY[n]: .10e}  hand {RRi[n] * dt * mean / r: .10e}"
                  f"  ratio {species[n]:.12f}")
        print(f"  sum dY {sum(dY.values()):.3e}  stepMean {mean:.12f}")
        prev = cur

    print(f"rho {rho:.12e}  ideal-gas hand {rho_hand:.12e}  eps {eps:g}")
    energies = [r[3] for r in rows]
    ratios = [v for r in rows for v in r[4].values()]
    out = case / "postProcessing" / FUNCTION / "0" / "gasReactionPorosity.dat"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        f"# eps {eps:g} rho {rho:.12e} A {A:g} beta {beta:g} Ta {Ta:g} dt {dt:g}\n"
        "# Time energyRatioMin energyRatioMax speciesRatioMin speciesRatioMax massChange\n"
        f"{rows[-1][0]:g} {min(energies):.12e} {max(energies):.12e} "
        f"{min(ratios):.12e} {max(ratios):.12e} "
        f"{max((r[5] for r in rows), key=abs):.6e}\n")


if __name__ == "__main__":
    main()
