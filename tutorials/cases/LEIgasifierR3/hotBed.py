"""Pre-built hot bed for LEIgasifierR3: the initial state, one table.

The bed starts from a steady downdraft profile instead of from cold: a hot
char zone at the bottom, a temperature ramp above it, cold wet wood on top.
Along the ramp the wood is dried and pyrolysed as far as a slow, steady
descent through it would have taken it (README.md, "Initial hot bed"):

    v_s dM/dz = k_dry(T) M        M: moisture left, of the fresh moisture
    v_s dX/dz = k_pyr(T) (1 - X)  X: wood converted to char and gas

integrated downwards from the cold top, with T(z) the assumed ramp and the
kinetics of constant/chemistryProperties. relax.py gives each sphere the
state at its packing height (and the matching radius) before settling;
writeInitialFields() then averages the settled spheres onto the mesh, so
the FVM fields and the DEM spheres start from the same bed.

    python3 hotBed.py            # writes 0/ (needs 0/C and the packing)
"""
from math import exp, pi
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import leiGeometry as lei

# Assumed profile (README.md). relax.py packs each row at its own radius, so
# these heights barely move when the bed settles.
T_hot     = 900.0     # K, char zone and the inert inflow
T_cold    = 300.0     # K, fresh feed
z_hot     = lei.z_plate + 0.05   # m, top of the char zone
ramp      = 0.20      # m, T falls from T_hot to T_cold over this height
v_solid   = 5e-4      # m/s, descent speed that built the profile
moisture  = 0.10      # wet-basis moisture of the fresh wood
z_fill    = lei.z_top - 0.15     # m, bed top, as R2 (demGeometry.z_fill)

# Kinetics and yields, as in constant/chemistryProperties.
A_dry, Ta_dry = 5.13e10, 10585.0
A_pyr, Ta_pyr = 1.38e10, 1.9e4
Tcrit         = 300.0
char_yield    = 0.2

# Densities, as in constant/solidThermophysicalProperties.
rho     = dict(wood=1050.0, char=1050.0, moisture=1000.0)
rhoApp  = dict(wood=630.0, char=229.0, moisture=1e5)
solid_fraction_fresh = 0.6     # bed solid fraction of fresh wood (R2's 0.4)


def T_at(z):
    s = min(1.0, max(0.0, (z - z_hot) / ramp))
    return T_hot + s * (T_cold - T_hot)


def k(A, Ta, T):
    return A * exp(-Ta / T) if T >= Tcrit else 0.0


def _profile(dz=1e-4):
    """(z, M, X) from the cold top down to the bottom of the bed."""
    z, M, X = z_fill + 0.05, 1.0, 0.0
    rows = [(z, M, X)]
    while z > lei.z_grate - 0.01:
        T = T_at(z)
        dt = dz / v_solid
        M *= exp(-k(A_dry, Ta_dry, T) * dt)
        X = 1.0 - (1.0 - X) * exp(-k(A_pyr, Ta_pyr, T) * dt)
        z -= dz
        rows.append((z, M, X))
    return rows[::-1]


_rows = _profile()


def state(z):
    """(M, X, T) at packing height z."""
    i = min(len(_rows) - 1, max(0, int((z - _rows[0][0]) / 1e-4)))
    return _rows[i][1], _rows[i][2], T_at(z)


def masses(M, X):
    """Solid masses (kg) per m3 of fresh particle: wood, char, moisture."""
    wood0 = rhoApp["wood"]
    return dict(wood=wood0 * (1.0 - X), char=char_yield * wood0 * X,
                moisture=wood0 * moisture / (1.0 - moisture) * M)


def volume_ratio(M, X):
    """Particle (apparent) volume over the fresh particle's: lambda^3."""
    def V(m):
        return sum(m[s] / rhoApp[s] for s in m)
    return V(masses(M, X)) / V(masses(1.0, 0.0))


def lam(M, X):
    return volume_ratio(M, X) ** (1.0 / 3.0)


def solid_fraction(M, X):
    """Bed solid fraction after re-packing at the fresh particle fraction."""
    def Vtrue(m):
        return sum(m[s] / rho[s] for s in m)
    return (solid_fraction_fresh * Vtrue(masses(M, X))
            / Vtrue(masses(1.0, 0.0)) / volume_ratio(M, X))


# ---------------------------------------------------------------------------
# 0/ fields from the settled spheres
# ---------------------------------------------------------------------------
def read_internal(path, n_cells=None):
    """internalField of an ascii field file: a list of floats or tuples."""
    txt = open(path).read()
    i = txt.index("internalField")
    body = txt[i:txt.index(";", i)]
    if "nonuniform" not in body:
        v = body.split("uniform", 1)[1].strip()
        v = tuple(map(float, v.strip("()").split())) if "(" in v else float(v)
        return [v] * n_cells
    body = txt[i:]
    n0 = body.index("(", body.index("nonuniform"))
    n = int(body[body.index(">", body.index("List<")) + 1:n0].split()[-1])
    vals = body[n0 + 1:].split("\n")
    out = []
    for line in vals:
        line = line.strip()
        if len(out) == n:
            break
        if line.startswith("("):
            out.append(tuple(float(v) for v in line.strip("()").split()))
        elif line and line != ")":
            out.append(float(line))
    return out


def _write_internal(path, values):
    txt = open(path).read()
    i = txt.index("internalField")
    j = txt.index(";", i)
    body = "internalField   nonuniform List<scalar>\n%d\n(\n%s\n)" % (
        len(values), "\n".join("%.10g" % v for v in values))
    open(path, "w").write(txt[:i] + body + txt[j:])


def cell_state(cells, spheres, reach=0.012):
    """Per cell (M, X, T, inBed): mean over spheres within reach, else nearest."""
    import bisect
    spheres = sorted(spheres, key=lambda s: s[0])
    xs = [s[0] for s in spheres]
    out = []
    for (cx, _, cz) in cells:
        lo = bisect.bisect_left(xs, cx - 0.03)
        hi = bisect.bisect_right(xs, cx + 0.03)
        near = [(s, (s[0] - cx) ** 2 + (s[1] - cz) ** 2) for s in spheres[lo:hi]]
        within = [s for s, d2 in near if d2 <= reach ** 2]
        if not within and near:
            s, d2 = min(near, key=lambda t: t[1])
            within = [s] if d2 <= (2.5 * reach) ** 2 else []
        if within:
            n = float(len(within))
            out.append((sum(s[3] for s in within) / n,
                        sum(s[4] for s in within) / n,
                        sum(s[5] for s in within) / n, True))
        else:
            out.append(None)
    return out


def writeInitialFields(case="."):
    cells = read_internal(os.path.join(case, "0", "C"))
    spheres = [tuple(map(float, l.split())) for l in
               open(os.path.join(case, "bedState.txt")) if not l.startswith("#")]
    top = max(s[1] + s[2] for s in spheres)
    # Spheres rest on the DEM-only grate; the pit below it holds no solid.
    st = [s if c[2] > lei.z_grate else None
          for c, s in zip(cells, cell_state(cells, spheres))]
    fields = {n: [] for n in ("Ywood", "Ychar", "Ymoisture", "porosityF", "Ts",
                              "T")}
    for (cx, _, cz), s in zip(cells, st):
        if s is not None:
            M, X, T, _ = s
        elif lei.z_grate < cz:
            # Above the settled bed: R2's static fresh freeboard, cold.
            M, X, T = 1.0, 0.0, T_cold
        else:
            M, X, T = None, None, T_hot   # ash pit: gas only, hot inflow
        if M is None:
            Y = dict(wood=0.0, char=0.0, moisture=0.0)
            eps = 1.0
        else:
            m = masses(M, X)
            Y = {k_: v / sum(m.values()) for k_, v in m.items()}
            eps = 1.0 - solid_fraction(M, X)
        fields["Ywood"].append(Y["wood"])
        fields["Ychar"].append(Y["char"])
        fields["Ymoisture"].append(Y["moisture"])
        fields["porosityF"].append(eps)
        fields["Ts"].append(T)
        fields["T"].append(T)
    for name, vals in fields.items():
        _write_internal(os.path.join(case, "0", name), vals)
    _write_internal(os.path.join(case, "0", "porosityF0"), fields["porosityF"])
    print("[HOTBED] %d cells, %d spheres, settled bed top %.3f m"
          % (len(cells), len(spheres), top))
    front = sorted(s[1] for s in spheres if 0.05 < s[4] < 0.95)
    if front:
        print("[HOTBED] pyrolysis zone (0.05 < X < 0.95) at z = %.3f .. %.3f m"
              % (front[0], front[-1]))


def describe():
    for z in [x / 100.0 for x in range(int(z_fill * 100), -1, -2)]:
        M, X, T = state(z)
        print("z %.2f  T %5.0f  M %.3f  X %.3f  lambda %.3f  solid %.3f"
              % (z, T, M, X, lam(M, X), solid_fraction(M, X)))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--describe":
        describe()
    else:
        writeInitialFields()
