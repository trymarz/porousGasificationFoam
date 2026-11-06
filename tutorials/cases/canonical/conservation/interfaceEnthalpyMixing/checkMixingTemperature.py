#!/usr/bin/env python3
"""Check that the gas temperature after each step is the mixing temperature of
the gas already in the cell and the gas the solid released at Ts (#212 D4).

    checkMixingTemperature.py PRODUCT TOL [CASE]

One cell, no interphase exchange, no pressure work, no gas reaction. With the
continuity the solver uses, outflow cancels, so per step, with m = porosityF*rho:

    (m_old + dt*S) * hs_mix(T_new, Y_new) = m_old*hs_mix(T_old, Y_old) + dt*S*hs_P(Ts_old)
    (m_old + dt*S) * Y_new = m_old*Y_old + dt*S*[P]

S is the step's Srho, all of it PRODUCT. hs is the JANAF sensible enthalpy of
constant/thermo.compressibleGas; rho is the perfect-gas rho the solver ends a
step with. Needs ddt Euler. Fails if |T - T_mix| > TOL [K] at any step, or if
the product's Y misses its mixing value by more than YTOL.
"""
import pathlib, re, sys

RR = 8314.462618  # J/kmol/K, OpenFOAM's constant::thermodynamics::RR
TSTD = 298.15
YTOL = 1e-6  # the product's Y against its mixing value: checks the rebuilt mass


def internal(path):
    m = re.search(r"internalField\s+uniform\s+([^;]+);", path.read_text())
    if not m:
        sys.exit(f"{path}: expected a uniform internalField (one cell)")
    return float(m.group(1))


def nonuniform_or_uniform(path):
    t = path.read_text()
    m = re.search(r"internalField\s+uniform\s+([^;]+);", t)
    if m:
        return float(m.group(1))
    m = re.search(r"internalField\s+nonuniform\s+List<scalar>\s*1\s*\(\s*([^)\s]+)\s*\)", t)
    if not m:
        sys.exit(f"{path}: cannot read a one-cell internalField")
    return float(m.group(1))


def janaf(case):
    t = (case / "constant" / "thermo.compressibleGas").read_text()
    sp = {}
    for m in re.finditer(r"\n(\w+)\s*\{(.*?)\n\}", t, re.S):
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


def mix_hs(sp, Y, T):
    return sum(Y[s] * hs(sp[s], T) for s in Y)


def solve_T(sp, Y, h, T0):
    T = T0
    for _ in range(100):
        f = mix_hs(sp, Y, T) - h
        cp = (mix_hs(sp, Y, T + 1e-3) - mix_hs(sp, Y, T - 1e-3)) / 2e-3
        dT = -f / cp
        T += dT
        if abs(dT) < 1e-12 * T:
            return T
    sys.exit("solve_T did not converge")


def main():
    product, tol = sys.argv[1], float(sys.argv[2])
    case = pathlib.Path(sys.argv[3] if len(sys.argv) > 3 else ".")
    sp = janaf(case)
    times = sorted((d for d in case.iterdir()
                    if d.is_dir() and re.fullmatch(r"[0-9.eE+-]+", d.name)),
                   key=lambda d: float(d.name))
    names = [s for s in sp if (times[0] / s).exists()]
    worst = worstY = 0.0
    for old, new in zip(times, times[1:]):
        dt = float(new.name) - float(old.name)
        Yo = {s: internal(old / s) for s in names}
        To, Tso = internal(old / "T"), internal(old / "Ts")
        po, eps = internal(old / "p"), internal(old / "porosityF")
        Wmix = 1 / sum(Yo[s] / sp[s][0] for s in names)
        m = eps * po * Wmix / (RR * To)
        S = nonuniform_or_uniform(new / "Srho")
        M = m + dt * S
        Ymix = {s: (m * Yo[s] + (dt * S if s == product else 0)) / M for s in names}
        hmix = (m * mix_hs(sp, Yo, To) + dt * S * hs(sp[product], Tso)) / M
        Tmix = solve_T(sp, Ymix, hmix, To)
        T = internal(new / "T")
        Yp = internal(new / product)
        err, errY = T - Tmix, Yp - Ymix[product]
        worst = max(worst, abs(err))
        worstY = max(worstY, abs(errY))
        print(f"t={new.name:>8}  T={T:.9f}  T_mix={Tmix:.9f}  T-T_mix={err:+.3e}  "
              f"Y_{product}-Y_mix={errY:+.3e}")
    print(f"max |T - T_mix| = {worst:.3e} K (tolerance {tol:g} K), "
          f"max |Y - Y_mix| = {worstY:.3e} (tolerance {YTOL:g})")
    return 0 if worst <= tol and worstY <= YTOL else 1


if __name__ == "__main__":
    sys.exit(main())
