"""Compare the one-step G and wall flux with the analytical grey slab (#238).

(1/3a) G'' - a G = -4 a sigma T^4, Marshak walls (emissivity 1, Ep = 1/2):
G = Gb + A cosh(k (x - L/2)), k = sqrt(3) a, and q = Ep (G(0) - Gw).
"""
import glob
import math
import re

a, L, n = 5.0, 0.1, 200
sigma = 5.670374419e-8
Gb, Gw = 4 * sigma * 1000.0**4, 4 * sigma * 500.0**4
gamma, k, Ep = 1 / (3 * a), math.sqrt(3) * a, 0.5
A = -Ep * (Gb - Gw) / (gamma * k * math.sinh(k * L / 2) + Ep * math.cosh(k * L / 2))
G_exact = [Gb + A * math.cosh(k * ((i + 0.5) * L / n - L / 2)) for i in range(n)]
q_exact = Ep * (Gb + A * math.cosh(k * L / 2) - Gw)

text = open("1e-06/G").read()
body = re.search(r"internalField\s+nonuniform\s+List<scalar>\s+\d+\s*\((.*?)\)\s*;", text, re.S)
G = [float(v) for v in body.group(1).split()]
assert len(G) == n, len(G)
G_err = max(abs(g - e) for g, e in zip(G, G_exact)) / (Gb - Gw)

path = glob.glob("postProcessing/regressionWallFlux/0/surfaceFieldValue.dat")[0]
q = float([l.split() for l in open(path) if not l.startswith("#")][-1][-1])
q_err = abs(q - q_exact) / q_exact

print(f"radiationGasSlab: max|G - G_exact|/(Gb - Gw) = {G_err:.3e}")
print(f"radiationGasSlab: wall flux {q:.6e} vs {q_exact:.6e} W/m2, rel err {q_err:.3e}")
raise SystemExit(0 if G_err < 1e-3 and q_err < 1e-3 else 1)
