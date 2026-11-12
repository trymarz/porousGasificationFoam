"""Check that the DEM walls and the FVM boundary are the same surface.

Every non-empty FVM boundary face (constant/polyMesh) must lie on a DEM
facet, and every DEM facet except the grate on an FVM boundary face. If
relaxedSpheres.txt exists, every sphere must also sit inside the bed region,
above the grate and in the slab mid-plane. Exits non-zero on any failure.

Usage, after blockMesh (and relax.py), from the case directory:
    yade -n -x checkDemWalls.py
"""
import os
import re
import sys

sys.path.insert(0, os.getcwd())

from yade import pack                # noqa: E402
from yade.system import O            # noqa: E402

import demGeometry as geom           # noqa: E402

TOL = 1e-9


def foam_list(path):
    """Body of an ascii OpenFOAM list file, one entry per line."""
    s = open(path).read()
    s = s[s.index("\n(", s.index("FoamFile")) + 2:]
    return s[:s.rindex("\n)")].split("\n")


def poly_mesh():
    pts = [tuple(map(float, l.strip("()").split()))
           for l in foam_list("constant/polyMesh/points") if l.strip()]
    faces = [list(map(int, l[l.index("(") + 1:l.index(")")].split()))
             for l in foam_list("constant/polyMesh/faces") if "(" in l]
    b = open("constant/polyMesh/boundary").read()
    patches = re.findall(r"(\w+)\s*\{\s*type\s+(\w+);[^}]*?nFaces\s+(\d+);"
                         r"\s*startFace\s+(\d+);", b)
    return pts, faces, patches


def tri_dist(p, a, b, c):
    """Distance from point p to triangle abc (Ericson, closest point)."""
    sub = lambda u, v: (u[0] - v[0], u[1] - v[1], u[2] - v[2])
    dot = lambda u, v: u[0] * v[0] + u[1] * v[1] + u[2] * v[2]
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        q = a
    else:
        bp = sub(p, b)
        d3, d4 = dot(ab, bp), dot(ac, bp)
        cp = sub(p, c)
        d5, d6 = dot(ab, cp), dot(ac, cp)
        vc, vb, va = d1 * d4 - d3 * d2, d5 * d2 - d1 * d6, d3 * d6 - d5 * d4
        if d3 >= 0 and d4 <= d3:
            q = b
        elif d6 >= 0 and d5 <= d6:
            q = c
        elif vc <= 0 and d1 >= 0 and d3 <= 0:
            t = d1 / (d1 - d3)
            q = tuple(a[i] + t * ab[i] for i in range(3))
        elif vb <= 0 and d2 >= 0 and d6 <= 0:
            t = d2 / (d2 - d6)
            q = tuple(a[i] + t * ac[i] for i in range(3))
        elif va <= 0 and d4 - d3 >= 0 and d5 - d6 >= 0:
            t = (d4 - d3) / ((d4 - d3) + (d5 - d6))
            q = tuple(b[i] + t * (c[i] - b[i]) for i in range(3))
        else:
            den = 1.0 / (va + vb + vc)
            v, w = vb * den, vc * den
            q = tuple(a[i] + ab[i] * v + ac[i] * w for i in range(3))
    return sum((p[i] - q[i]) ** 2 for i in range(3)) ** 0.5


def on_any(p, tris):
    return min(tri_dist(p, *t) for t in tris) < TOL


def sample(poly):
    """Vertices, edge midpoints and centroid of a polygon."""
    n = len(poly)
    mids = [tuple((poly[i][k] + poly[(i + 1) % n][k]) / 2 for k in range(3))
            for i in range(n)]
    cen = tuple(sum(v[k] for v in poly) / n for k in range(3))
    return list(poly) + mids + [cen]


failures = []
pts, faces, patches = poly_mesh()
fvm_tris, fvm_polys = [], []
for name, typ, n, start in patches:
    if typ == "empty":
        continue
    for f in faces[int(start):int(start) + int(n)]:
        poly = [pts[i] for i in f]
        fvm_polys.append((name, poly))
        fvm_tris += [(poly[0], poly[i], poly[i + 1]) for i in range(1, len(poly) - 1)]

geom.add_materials()
geom.add_walls()
grate = geom.grate_facets()
dem_tris = []
for b in O.bodies:
    v = [tuple(b.state.pos + b.state.ori * x) for x in b.shape.vertices]
    if max(p[1] for p in v) - min(p[1] for p in v) < TOL:
        continue                       # slab faces: FVM empty patch
    dem_tris.append(tuple(v))

for name, poly in fvm_polys:
    if not all(on_any(p, dem_tris) for p in sample(poly)):
        failures.append("FVM face of %s not on a DEM facet: %s" % (name, poly))
key = lambda t: tuple(sorted(tuple(round(c, 12) for c in p) for p in t))
grate_keys = {key(g) for g in grate}
nGrate = 0
for t in dem_tris:
    if key(t) in grate_keys:
        nGrate += 1
        continue
    if not all(on_any(p, fvm_tris) for p in sample(list(t))):
        failures.append("DEM facet off the FVM boundary: %s" % (t,))
print("[WALLS] %d FVM boundary faces, %d DEM facets (%d grate, DEM only)"
      % (len(fvm_polys), len(dem_tris), nGrate))

if os.path.exists("relaxedSpheres.txt"):
    sp = pack.SpherePack()
    sp.load("relaxedSpheres.txt")
    zg = geom.outline[0][1]
    worst = float("inf")
    for c, r in sp:
        x, y, z = c
        gap = geom.wall_distance(x, z) / r
        worst = min(worst, gap)
        if not geom.inside(x, z) or z < zg or abs(y) > TOL:
            failures.append("sphere outside the bed region: %s" % (c,))
    print("[SPHERES] %d spheres, closest wall at %.3f radii" % (len(sp), worst))
    if worst < 0.9:
        failures.append("a sphere overlaps a wall by more than 10%% of r")

for f in failures[:20]:
    print("[FAIL]", f)
print("[CHECK]", "FAILED (%d)" % len(failures) if failures else "OK")
sys.exit(1 if failures else 0)
