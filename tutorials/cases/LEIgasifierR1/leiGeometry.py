"""LEI downdraft reactor outline for LEIgasifierR1: one source for mesh and DEM.

Planar-2D xz cut through the reactor axis, z up, z = 0 on the grate, slab
y in [-half_depth, half_depth]. Published dimensions and layout are from
Striugas et al., Appl. Therm. Eng. 73 (2014) 1151-1159, Sec. 2.2 and Fig. 3;
every other number is an assumption and is listed in README.md. A git-ignored
constant/LEIgeometry.local overrides them with the site drawing (README.md).

    python3 leiGeometry.py      # writes system/blockMeshDict

relax.py, MPI_lambda.py and checkDemWalls.py import this module, so the DEM
walls, the sphere packing region and the FVM boundary come from one table.
"""
import ast
from pathlib import Path

half_depth = 0.008          # slab half-thickness (m), as in LEIgasifierDEM

# Published (Striugas et al. 2014, Sec. 2.2), as half-widths and heights (m).
r_shaft     = 0.200         # reactor above the throat, 400 mm
r_tuyere    = 0.100         # tuyere position, 200 mm
r_throat    = 0.065         # throat, 130 mm
r_reduction = 0.150         # reduction zone, 300 mm
h_reduction = 0.430         # reduction zone height
h_tuyere    = 0.110         # tuyeres to throat
h_upper     = 0.600         # upper part of the reactor

# Assumed (README.md, "Assumptions"): what neither the text nor Fig. 3 sizes.
primary_on  = "cone"        # "cone": top row of the tuyere cone; "shaft": wall
h_primary   = 0.020         # primary port height
z_primary   = 0.0           # shaft ports only: port centre above the plate
h_pit       = 0.150         # ash pit under the grate
r_pit_top   = "chamber"     # pit top: "chamber" (a cone) or "throat" (a step)
r_pit_out   = 0.075         # pit bottom, where the secondary supply enters
z_outlet    = (0.050, 0.110)  # side gas outlet, chamber wall
outlet_side = 1             # +1 right wall, -1 left wall
t_cone      = 0.010         # tuyere cone wall, when the pocket is meshed
dz_cell     = 0.022         # target cell height (m)
dx_cell     = 0.018         # target cell width (m)

# Site data stays out of git: constant/LEIgeometry.local overrides the values
# above by name, as Python literals ("name = value"). See README.md.
LOCAL = Path(__file__).resolve().parent / "constant" / "LEIgeometry.local"
overridden = []
if LOCAL.exists():
    for node in ast.parse(LOCAL.read_text()).body:
        name = node.targets[0].id
        if name not in globals() or name.startswith("_") or name == "LOCAL":
            raise SystemExit("LEIgeometry.local: unknown name %r" % name)
        globals()[name] = ast.literal_eval(node.value)
        overridden.append(name)

# Fig. 3: the shaft's bottom plate is the chamber top; the tuyere cone hangs
# from it into the chamber, so the throat sits h_tuyere below the plate.
z_grate  = 0.0
z_plate  = z_grate + h_reduction
z_throat = z_plate - h_tuyere
z_top    = z_plate + h_upper
z_pit    = z_grate - h_pit
pocket   = z_outlet[1] > z_throat   # outlet beside the cone: mesh the pocket
if primary_on == "cone":
    z_port = (z_plate - h_primary, z_plate)
else:
    z_port = (z_plate + z_primary - h_primary / 2,
              z_plate + z_primary + h_primary / 2)
assert primary_on in ("cone", "shaft") and r_pit_top in ("chamber", "throat")
assert z_grate < z_outlet[0] < z_outlet[1] < z_plate and outlet_side in (1, -1)
assert z_plate < z_port[0] or primary_on == "cone"


def _r_cone(z):
    return r_throat + (r_tuyere - r_throat) * (z - z_throat) / h_tuyere


def _sym(rs):
    return [-r for r in reversed(rs)] + list(rs)


def _cuts(z0, z1, extra):
    return [z0] + sorted(z for z in set(extra) if z0 < z < z1) + [z1]


# Chamber columns; with a meshed pocket, a column of width t_cone lines up
# with the cone wall above it.
_chamber = _sym([r_throat, r_throat + t_cone, r_reduction] if pocket
                else [r_throat, r_reduction])
_centre = len(_chamber) // 2 - 1
_shaft = _sym([r_tuyere, r_shaft])


def _cone_edges(z):
    rc = _r_cone(z)
    return _sym([rc, rc + t_cone, r_reduction]) if pocket else [-rc, rc]


# x cells per column: fixed per column role, so matched block faces agree.
_n = lambda w: max(1, round(w / dx_cell))
_nx_centre = [_n(2 * r_throat)]
_nx_chamber = (_n(r_reduction - r_throat - t_cone), 1) if pocket \
    else (_n(r_reduction - r_throat),)
_nx_chamber = list(_nx_chamber) + _nx_centre + list(reversed(_nx_chamber))
_nx_shaft = [_n(r_shaft - r_tuyere)] + _nx_centre + [_n(r_shaft - r_tuyere)]

# Layers bottom to top: (z0, z1, x-edges at z0, x-edges at z1, x cells per
# column, solid columns).
if r_pit_top == "chamber":
    _pit_top, _nx_pit = _chamber, _nx_chamber
else:
    _pit_top, _nx_pit = [-r_throat, r_throat], _nx_centre
layers = [(z_pit, z_grate, [e * r_pit_out / _pit_top[-1] for e in _pit_top],
           _pit_top, _nx_pit, ())]
_cut = _cuts(z_grate, z_throat, z_outlet)
layers += [(a, b, _chamber, _chamber, _nx_chamber, ())
           for a, b in zip(_cut, _cut[1:])]
_cut = _cuts(z_throat, z_plate, list(z_outlet) +
             ([z_port[0]] if primary_on == "cone" else []))
layers += [(a, b, _cone_edges(a), _cone_edges(b),
            _nx_chamber if pocket else _nx_centre, (1, 3) if pocket else ())
           for a, b in zip(_cut, _cut[1:])]
_cut = _cuts(z_plate, z_top, z_port if primary_on == "shaft" else ())
layers += [(a, b, _shaft, _shaft, _nx_shaft, ())
           for a, b in zip(_cut, _cut[1:])]

# Bed region above the grate, counter-clockwise in xz (DEM packing domain).
bed_outline = [
    (-r_reduction, z_grate), (r_reduction, z_grate),
    (r_reduction, z_throat), (r_throat, z_throat),
    (r_tuyere, z_plate), (r_shaft, z_plate), (r_shaft, z_top),
    (-r_shaft, z_top), (-r_shaft, z_plate), (-r_tuyere, z_plate),
    (-r_throat, z_throat), (-r_reduction, z_throat),
]

# The grate: a DEM-only wall. The gas passes it, so it is no FVM boundary.
grate = ((-r_reduction, z_grate), (r_reduction, z_grate))

# Outward hex faces of OpenFOAM's hex model, local axes (x, y, z).
_hex_faces = {"xmin": (0, 4, 7, 3), "xmax": (1, 2, 6, 5),
              "ymin": (0, 1, 5, 4), "ymax": (3, 7, 6, 2),
              "zmin": (0, 3, 2, 1), "zmax": (4, 5, 6, 7)}


def _patch(side, z0, z1, x_mean):
    """Patch of an unmatched block face, from where it sits."""
    if side in ("ymin", "ymax"):
        return "frontAndBack"
    if side == "zmin" and z0 == z_pit:
        return "secondaryInlet"
    if side not in ("xmin", "xmax"):
        return "wall"
    wall_out = (side == "xmax") == (x_mean > 0)   # faces away from the axis
    at_chamber = abs(abs(x_mean) - r_reduction) < 1e-9
    if (z0, z1) == z_port and wall_out and (z0 >= z_plate or not at_chamber):
        return "primaryInlet"
    if (z0, z1) == tuple(z_outlet) and at_chamber and x_mean * outlet_side > 0:
        return "outlet"
    return "wall"


def mesh_blocks():
    """Vertices, hex blocks and boundary faces {patch: [face]}."""
    verts, index, blocks, owner = [], {}, [], {}

    def vid(x, y, z):
        key = (round(x, 9), round(y, 9), round(z, 9))
        if key not in index:
            index[key] = len(verts)
            verts.append(key)
        return index[key]

    h = half_depth
    for z0, z1, xb, xt, nxs, solid in layers:
        ncol = len(xb) - 1
        nz = max(1, round((z1 - z0) / dz_cell))
        for c in range(ncol):
            if c in solid:
                continue
            v = [vid(xb[c], -h, z0), vid(xb[c + 1], -h, z0),
                 vid(xb[c + 1], h, z0), vid(xb[c], h, z0),
                 vid(xt[c], -h, z1), vid(xt[c + 1], -h, z1),
                 vid(xt[c + 1], h, z1), vid(xt[c], h, z1)]
            nx = nxs[c]
            blocks.append((v, (nx, 1, nz)))
            for side, loc in _hex_faces.items():
                f = [v[i] for i in loc]
                key = tuple(sorted(f))
                xm = sum(verts[i][0] for i in f) / 4
                owner[key] = None if key in owner else (side, z0, z1, xm, f)

    faces = {}
    for entry in owner.values():
        if entry:
            side, z0, z1, xm, f = entry
            faces.setdefault(_patch(side, z0, z1, xm), []).append(f)
    return verts, blocks, faces


def write_block_mesh_dict(path):
    verts, blocks, faces = mesh_blocks()
    types = {"wall": "wall", "frontAndBack": "empty"}
    out = ["FoamFile", "{", "    version     2.0;", "    format      ascii;",
           "    class       dictionary;", "    object      blockMeshDict;",
           "}", "", "// Written by leiGeometry.py; edit that, not this.", "",
           "vertices", "("]
    out += ["    (%.9g %.9g %.9g) // %d" % (*p, i) for i, p in enumerate(verts)]
    out += [");", "", "blocks", "("]
    out += ["    hex (%s) (%d %d %d) simpleGrading (1 1 1)"
            % (" ".join(map(str, v)), *n) for v, n in blocks]
    out += [");", "", "edges", "(", ");", "", "boundary", "("]
    for name in sorted(faces):
        out += ["    " + name, "    {",
                "        type %s;" % types.get(name, "patch"),
                "        faces", "        ("]
        out += ["            (%s)" % " ".join(map(str, f)) for f in faces[name]]
        out += ["        );", "    }"]
    out += [");", "", "mergePatchPairs", "(", ");", ""]
    Path(path).write_text("\n".join(out))


if __name__ == "__main__":
    case = Path(__file__).resolve().parent
    write_block_mesh_dict(case / "system" / "blockMeshDict")
    print("Wrote", case / "system" / "blockMeshDict")
    print("Local overrides:", ", ".join(overridden) if overridden else "none")
