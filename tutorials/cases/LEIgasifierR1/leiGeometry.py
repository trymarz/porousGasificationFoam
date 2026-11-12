"""LEI downdraft reactor outline for LEIgasifierR1: one source for mesh and DEM.

Planar-2D xz cut through the reactor axis, z up, z = 0 on the grate, slab
y in [-half_depth, half_depth]. Published dimensions and layout are from
Striugas et al., Appl. Therm. Eng. 73 (2014) 1151-1159, Sec. 2.2 and Fig. 3;
every other number is an assumption and is listed in README.md.

    python3 leiGeometry.py      # writes system/blockMeshDict

relax.py, MPI_lambda.py and checkDemWalls.py import this module, so the DEM
walls, the sphere packing region and the FVM boundary come from one table.
"""
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
h_primary   = 0.020         # primary port: top cell row of the tuyere cone
h_cone      = 0.150         # ash cone under the grate
r_cone_out  = 0.075         # cone bottom, where the secondary supply enters
z_outlet    = (0.050, 0.110)  # side gas outlet, right chamber wall

# Fig. 3: the shaft's bottom plate is the chamber top; the tuyere cone hangs
# from it into the chamber, so the throat sits h_tuyere below the plate.
z_grate  = 0.0
z_plate  = z_grate + h_reduction
z_throat = z_plate - h_tuyere
z_top    = z_plate + h_upper
z_cone   = z_grate - h_cone

NX_SIDE, NX_CENTRE = 4, 8   # x cells per side and centre column


def _scaled(edges, r):
    return [e * r / edges[-1] for e in edges]


# Layers bottom to top: (z0, z1, x-edges at z0, x-edges at z1, nz).
_chamber = [-r_reduction, -r_throat, r_throat, r_reduction]
_shaft = [-r_shaft, -r_tuyere, r_tuyere, r_shaft]
_r_primary = r_throat + (r_tuyere - r_throat) * (1 - h_primary / h_tuyere)
layers = [
    (z_cone, z_grate, _scaled(_chamber, r_cone_out), _chamber, 6),
    (z_grate, z_outlet[0], _chamber, _chamber, 2),
    (z_outlet[0], z_outlet[1], _chamber, _chamber, 3),
    (z_outlet[1], z_throat, _chamber, _chamber, 9),
    (z_throat, z_plate - h_primary, [-r_throat, r_throat],
     [-_r_primary, _r_primary], 4),
    (z_plate - h_primary, z_plate, [-_r_primary, _r_primary],
     [-r_tuyere, r_tuyere], 1),
    (z_plate, z_top, _shaft, _shaft, 24),
]

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
    if side == "zmin" and z0 == z_cone:
        return "secondaryInlet"
    if side in ("xmin", "xmax") and (z0, z1) == (z_plate - h_primary, z_plate):
        return "primaryInlet"
    if side == "xmax" and (z0, z1) == z_outlet and x_mean > 0:
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
    for z0, z1, xb, xt, nz in layers:
        ncol = len(xb) - 1
        for c in range(ncol):
            v = [vid(xb[c], -h, z0), vid(xb[c + 1], -h, z0),
                 vid(xb[c + 1], h, z0), vid(xb[c], h, z0),
                 vid(xt[c], -h, z1), vid(xt[c + 1], -h, z1),
                 vid(xt[c + 1], h, z1), vid(xt[c], h, z1)]
            nx = NX_CENTRE if ncol == 1 or c == 1 else NX_SIDE
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
