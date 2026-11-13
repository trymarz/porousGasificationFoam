# LEIgasifierR2 — feed and discharge, cold

Rung R2 of issue #64. It is a sibling of `LEIgasifierR1` that changes only one thing: the bed now moves. Spheres are inserted at the top of the shaft at a fixed, assumed rate and removed at the grate at the same rate, and FoamCoupling's id list follows them. Everything else is R1: the outline, the mesh, the cold inert N2 flow, chemistry and pyrolysis off.

Geometry, sources, the site-drawing file, assumptions 1–9 and the planar-2D flow mapping are R1's, unchanged; see `../LEIgasifierR1/README.md`. `leiGeometry.py` here is the same table, and reads `constant/LEIgeometry.local` the same way (git-ignored, never committed).

## Feed and discharge

All values are in `demGeometry.py`.

| Item | Value | Status |
|---|---|---|
| Feed rate | 100 spheres/s = 0.02815 kg/s per 16 mm slab (2.815e-4 kg per 8 mm sphere at 1050 kg/m³) | assumed |
| Discharge rate | equal to the feed rate | assumed |
| Initial bed top `z_fill` | 150 mm below the lid; R1 packed the bed to the lid | assumed |
| Feed band | 20–60 mm below the lid, \|x\| ≤ r_shaft − 1.5 r, random free spots (seeded) | assumed |
| Discharge band | sphere centres less than 1.5 r above the grate, lowest first | assumed |

**The feed rate is a numerical turnover rate, not LEI's.** The bed holds about 1.44 kg of pellets per slab (5119 spheres), so 100 spheres/s turns it over in about 51 s. That is far faster than a real gasifier, where the bed holds on the order of an hour of fuel (#64), and it is chosen so that a run of a few seconds moves the bed visibly. #64 derives the real rate from an assumed O2 equivalence ratio. That belongs to R4/R5 together with open decision 1 (physical time to "stable"), and the rate here is replaced then.

**Discharge is a continuous equivalent of the ash screw** (#64, open decision 3), at the feed rate. With chemistry off nothing shrinks, so the cold mass balance needs out = in: a free discharge, taking whatever reaches the grate, would drain the bed. When too few spheres sit on the grate, the shortfall carries over to the next step and shows up as a backlog in `balance.txt`.

**Bookkeeping.**

- The feed spheres are created before the run, parked far outside the mesh with every DOF blocked, and released into the feed band when due: `FoamCoupling.insertBodyId` adds them to the coupling. Creating bodies inside the split MPI scene isn't supported by `yade.mpy`, so the run needs exactly one Yade worker (2 Yade ranks) and stops otherwise.
- Discharged spheres are taken out of the coupling with `FoamCoupling.eraseId` and parked the same way. They aren't erased: `mp.bodyErase()` leaves FoamCoupling a stale per-domain id, and the next `setHydroForce()` dereferences a null body (a segfault in this Foam-Yade build). For the same reason, feed and discharge run only on the step before a coupling exchange, which rebuilds those ids.
- `changeRadius()` parks spheres that shrink below 0.1 mm the same way (counted as "burnt"). With R2 cold, none do.
- Every 0.01 s `MPI_lambda.py` appends a row to `balance.txt`: time, sphere count, coupled-id count, the counts and masses in, out and burnt, the inventory, the residual `m_in − m_out − m_burnt − (inventory − inventory₀)`, and the bed level. The bed level is the mean height of the top layer: the shaft width is cut into columns one sphere diameter wide, each column contributes the top of its highest sphere that is below the feed band and moving slower than 0.1 m/s, and the level is the mean over the columns. A heap under the feed point then moves material without moving the level; only a change in inventory does. A coupled-id count that differs from the sphere count stops the run, and so does a feed spot that stays blocked (the bed has reached the feed band).
- The FVM side is unchanged from R1. Its porosity is the static fresh-bed field, 0.4 above the grate, so the 150 mm freeboard is still porous to the gas. The spheres reach OpenFOAM only through Us, as in R1.

## Running

```sh
./Allrun    # mesh, checkMesh, setFields, relax.py, checkDemWalls.py, coupled run to 2.5 s, checkFeed.py
./Allclean
```

The coupled run takes about 8–9 minutes (2 Yade + 2 OpenFOAM ranks). `checkFeed.py` checks the run afterwards:

- solid mass in = out + inventory change at every row, to 1e-12 kg;
- the coupled-id count equals the sphere count at every row;
- the feed count matches the rate, and the discharge backlog stays at 5 spheres or fewer;
- the bed level moves less than two sphere diameters over the last half of the run;
- gas outflow equals inflow to 1e-3 over the last half.

For a CFD-only cold flow, use R1's procedure; R2 doesn't change the gas side.

## Checks

A 2.5 s run with `pgf run .` (Foam-Yade d5c33e0, 2 OpenFOAM ranks, 9 min):

- `checkMesh`: Mesh OK. `checkDemWalls.py`: 5119 spheres, closest wall at 0.936 r, OK.
- Feed and discharge: 249 in, 249 out (rate·t = 249), backlog never above 0 spheres; coupled-id count equal to the sphere count at every row.
- Mass balance: `in − out − Δinventory` is exactly 0 at all 250 rows; the inventory stays constant.
- Bed level (top-layer mean): rises 18 mm (4.6 r) over the run, with a 2.7 r range over the last half (limit 4 r). It rises about 2 mm per 0.25 s for the first 1.5 s, then flattens, although the inventory is constant. The likely cause is the bed loosening as the discharge shears it, but this has not been checked. A longer run would show whether the level settles.
- Cold flow: outflow matches inflow to 3.2e-5 over the last half.

The level is measured with the top-layer mean because the previous metric, the highest resting sphere, failed the 4 r limit (6.8 r) on the same run: the heap under the feed point moved the peak, not the level.

The case is not in `applications/test/regression/cases.list` or `cases_yade.list`.
