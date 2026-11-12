# LEIgasifierR1 — LEI reactor outline, cold

Rung R1 of issue #64. It is a sibling of `LEIgasifierDEM` that changes only the geometry. The case puts the outline of the LEI downdraft reactor into the planar-2D twin-domain setup and runs it cold: the gas is inert N2, chemistry and pyrolysis are off, and the spheres settle with `relax.py`. Later rungs build on this one: feed and discharge (R2), heat (R3), O2 (R4) and the wood run (R5).

## Source

Striūgas N., Zakarauskas K., Džiugys A., Navakas R., Paulauskas R., *An evaluation of performance of automatically operated multi-fuel downdraft gasifier for energy production*, Appl. Therm. Eng. 73 (2014) 1151–1159, doi:10.1016/j.applthermaleng.2014.09.007. The current LEI unit is a redesign of this reactor (#64). Every number below that the paper doesn't give is marked as an assumption.

Published values used (Sec. 2.2, Tables 1–3):

| Item | Value | Where |
|---|---|---|
| Reactor diameter above the throat (upper shaft) | 400 mm | Sec. 2.2 |
| Diameter at the tuyere position | 200 mm | Sec. 2.2 |
| Tuyeres to throat | 110 mm | Sec. 2.2 |
| Throat diameter | 130 mm | Sec. 2.2 |
| Reduction zone diameter / height | 300 mm / 430 mm | Sec. 2.2 |
| Height of the upper part | 600 mm | Sec. 2.2 |
| Moving grate under the reduction zone; screw discharge | — | Sec. 2.2, Fig. 3 |
| Softwood pellets, 8 mm | sphere radius 4 mm | Table 1 |
| Primary / secondary air, softwood run | 30 / 5 Nm³/h | Table 3 |

Layout read from Fig. 3: the shaft's bottom plate is the top of the reduction chamber. The tuyere cone hangs from the 200 mm opening in that plate and narrows to the 130 mm throat over 110 mm, so the throat sits inside the chamber, 320 mm above the grate. The grate is the chamber floor.

### Site drawing (local, not in the repository)

LEI's drawing of the current reactor is confidential, so none of its numbers are in this repository. Put them in `constant/LEIgeometry.local`, which git ignores (`tutorials/cases/**/*.local`). The file holds Python literal assignments (`name = value`) that override the values of the same name at the top of `leiGeometry.py`; an unknown name stops the script. Without the file, the case runs on the 2014 values and the assumptions below. `system/blockMeshDict` is generated, so it is git-ignored too.

The drawing decides the heights, the port and outlet positions and the outline. The paper supplies the internals the drawing doesn't show: the throat, the tuyere cone and all diameters. Where the two disagree, the drawing wins. The disagreements, by role:

- **Upper shaft height:** the drawing's value differs from the paper's upper-part height.
- **Primary ports:** the drawing puts them in the shaft wall, a short distance above the bottom plate (`primary_on = "shaft"`). The paper puts the tuyeres at the 200 mm section, at the top of the tuyere cone.
- **Gas outlet:** the drawing puts it on the other side (`outlet_side = -1`), high in the lower chamber, above the throat. The pocket between the tuyere cone and the chamber wall is then meshed, and the cone gets a wall of assumed thickness `t_cone`.
- **Under the grate:** the drawing shows a straight chamber narrower than the reduction zone (`r_pit_top = "throat"`), not a cone. Its width isn't dimensioned, so it takes the throat width.
- **Lower drum width:** the drawing shows the lower drum wider than the upper shaft; in the paper the reduction zone is narrower than the shaft. The drawing gives no width, so the paper's diameters stay. The drum may be the casing around the 2014 lining.
- **Throat:** the drawing shows a plain step with no throat. The 2014 internals stay (#64, open decision 2).

The drawing's thermocouple heights aren't used yet (assumption 9).

## Geometry

`leiGeometry.py` is the only geometry table. It writes `system/blockMeshDict`, and the DEM scripts import it, so the mesh, the DEM walls and the packing region can't drift apart. The slab is the xz-plane through the reactor axis, z = 0 on the grate, 16 mm deep (y = ±8 mm) as in `LEIgasifierDEM`. Without the local file:

| Region | z (m) | Half-width (m) |
|---|---|---|
| Ash cone (gas only) | −0.15 → 0 | 0.075 → 0.15 |
| Reduction chamber | 0 → 0.32 | 0.15 |
| Tuyere cone | 0.32 → 0.43 | 0.065 → 0.10 |
| Upper shaft | 0.43 → 1.03 | 0.20 |

Cell sizes come from target widths and heights (`dx_cell`, `dz_cell`), so the local file doesn't need a mesh of its own. Patches are `primaryInlet`, `secondaryInlet`, `outlet`, `wall` and `frontAndBack` (empty). The grate is a DEM wall only: gas passes through it, so the FVM mesh has no face there.

## Assumptions

1. **The throat is kept.** Neither the text nor Fig. 3 says whether the redesign keeps it (#64, open decision 2), so R1 uses the 2014 internals.
2. **Primary inlets:** the top cell row (20 mm) of the tuyere cone, on both sides, injecting normal to the cone wall. The paper gives the tuyere diameter but not the port geometry. It also notes a working diameter of about 250 mm, because spent pellets insulate the wall; R1 uses the steel diameter of 200 mm.
3. **Secondary supply from below the bed** (#64): the bottom of the ash cone. In 2014 the secondary air entered through the chamber side near the grate, and the tertiary air came up a central rotating pipe (Fig. 3). Neither is modelled, and the tertiary supply is dropped.
4. **Side gas outlet:** right chamber wall, 50–110 mm above the grate. In 2014 the gas left under the grate and rose through an annulus between the chamber wall and the brick lining to a port near the chamber top (Fig. 3). #64 places the redesign's outlet on the side of the lower chamber, and its height isn't published.
5. **Ash cone:** 150 mm tall, narrowing from the 300 mm chamber to 150 mm at the bottom. Fig. 3 shows a straight ash zone with no dimensions; #64 asks for a cone.
6. **The pocket between the tuyere cone and the chamber wall is meshed only when the outlet reaches above the throat.** Otherwise the chamber's top boundary is the throat level (a ledge at z = 0.32 m), and the pocket is dead space above the char pile. When it is meshed, the cone wall is 10 mm thick (`t_cone`). The pocket is outside the packing region, so it starts empty.
7. **The upper part's 600 mm** runs from the bottom plate to the lid. The fuel level, the feed opening and the P/L sensors are not modelled; the lid is a wall.
8. **Inert gas and flows:** N2 at 300 K replaces air, at the softwood run's Table 3 normal flows. Fresh-bed porosity is 0.4 (as `LEIgasifierDEM`), and the Darcy coefficient is Ergun's viscous term for 8 mm pellets at that porosity, 1.3e7 m⁻². There is no Forchheimer term.
9. **Thermocouple and sampling positions** (T1–T6, Fig. 3) are not placed. #64 leaves them to R5.

## Planar-2D flow mapping

This rule is written down here once, and every later rung reuses it.

> A 3D volumetric flow Q through a circular section of diameter D maps to the slab flow q = Q · (D · b) / (π D² / 4), with b the slab depth. That is, Q is scaled by the ratio of the slab's cross-section at that level to the reactor's. Each supply uses the section where it enters the bed: primary at the tuyere diameter (200 mm), secondary at the reduction-zone diameter (300 mm).

Superficial velocity is therefore matched at each supply's own reference section. Elsewhere, at a section of diameter D, the slab's superficial velocity is D/D_ref times the 3D value: a planar slab narrows linearly while a pipe narrows quadratically. In R1 the cold throat sees 0.65× the 3D superficial velocity of the primary stream.

Normal flows are converted to mass flows with N2 at 0 °C and 101325 Pa (ρ = 1.2498 kg/m³) and imposed with `flowRateInletVelocity`:

| Supply | Q (Nm³/h) | D_ref (m) | factor D·b/A | q (Nm³/s) | ṁ (kg/s) |
|---|---|---|---|---|---|
| primary | 30 | 0.20 | 0.10186 | 8.488e-4 | 1.0609e-3 |
| secondary | 5 | 0.30 | 0.06791 | 9.431e-5 | 1.1788e-4 |

R4 maps the O2 split with the same factors.

## Running

```sh
./Allrun    # mesh, checkMesh, setFields, relax.py, checkDemWalls.py, coupled smoke run
./Allclean
```

For the cold-flow check, run CFD only: set `active false` in `constant/yadeProperties` and `endTime 10` in `system/controlDict`, then run `porousGasificationFoam` serially after `setFields`.

## Checks

Without the local file:

- **Mesh quality:** `checkMesh` reports `Mesh OK`. There are 922 hexahedra, max non-orthogonality is 25.3°, max skewness 1.18 and max aspect ratio 2.0. All faces are in named patches, with none in `defaultFaces`.
- **DEM walls agree with the FVM boundary:** `checkDemWalls.py` loads the DEM walls the way the run does. It then checks two things: each vertex, edge midpoint and centroid of every non-empty `constant/polyMesh` boundary face lies on a DEM facet, and every DEM facet except the grate lies on the FVM boundary, both to within 1e-9 m. After relaxation it also checks that every sphere sits inside the bed region, above the grate and on the mid-plane, with less than 10% overlap with a wall. Moving one blockMeshDict vertex by 5 mm makes it fail.
- **Settling:** 6138 spheres settle in 9500 DEM steps (0.475 s): unbalanced force 2e-4, fastest sphere 0.7 mm/s, closest wall 0.93 r.
- **Cold flow:** a CFD-only run to 10 s (2500 steps at 4e-3 s) is steady from about 2 s. Axis-probe pressures don't change from 2 s to 10 s at the probes' written precision, max |U(10 s) − U(9 s)| is 2.4e-7 m/s, max |U| is 1.11 m/s at the primary ports and max Courant number is 0.34. Outflow equals inflow to 2.6e-6 relative, T stays within 300–300.05 K, and the pressure drop from the shaft to the chamber bottom is about 23 Pa. This is not a target: the paper's 3 kPa set point is for hot operation on a char bed.

With the local site file the same checks pass: `Mesh OK`, the wall and sphere check OK, and a stable cold flow with outflow equal to inflow to 6e-6. A slow recirculation under the grate is still decaying at 10 s: the per-second velocity change falls about 2.5× each second.

The full `Allrun`, including the coupled smoke run on 4 ranks, finishes cleanly in about a minute with and without the local file. The coupled smoke run checks only that the coupling starts and ends cleanly. With chemistry off, lambdaDot is zero and the spheres keep their size.

The case is not in `applications/test/regression/cases.list` or `cases_yade.list`.
