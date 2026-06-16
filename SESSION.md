# Session handoff — 2026-06-16

**Carry forward to next session.**

## What we did

Changed the posterDemo Yade sphere packing from random `makeCloud` (250 spheres,
r=0.003, ~18% solid fraction, collapsed under gravity) to a regular
`pack.regularOrtho` grid (160 spheres, r=0.005, 52% solid fraction,
self-supporting).

**File changed:** `tutorials/cases/posterDemo/MPI_lambda.py` (uncommitted)

## What was verified (3 test runs to t=0.006, 0.178, 0.334)

- ✅ lambdaDot field computed and written to OF time dirs (0.05–0.30)
- ✅ Spheres shrink from lambdaDot: mean r 0.00293 → 0.00267 (t=0.05→0.25)
- ✅ Master-slave sync working (rank0/rank1 VTK radii identical)
- ✅ Ywood: 0.99 → 0.27 at t=0.334 (73% consumed)
- ✅ T gas: 569–691 K
- ✅ deltaT grows from 5e-4 to 0.001 s
- ✅ VTK output: 5 frames (0.05–0.25)
- ❌ Spheres collapsed to floor (random packing doesn't interlock) — **fixed**
- ❌ Case stopped 3 times (terminal disconnect on `tee log.yade`) — **use nohup**

## What to do next

```
# 1. Rebuild solver on KVM host (container build fails: missing FoamYade.H)
./build.sh build --yade

# 2. Clean old run artifacts from posterDemo
cd tutorials/cases/posterDemo
rm -rf processor* spheres/ log.yade [0-9]*.[0-9]* 0

# 3. Run backgrounded (terminal-proof)
nohup ./Allrun > run.log 2>&1 &
echo $! > run.pid

# 4. Monitor every 60s
watch -n 60 'grep "^Time =" log.yade | tail -1; grep "Ywood.*max Y" log.yade | tail -1'
```

**Expected:** t=2s in ~100 min. 40 VTK frames, 40 OF time dirs.

**After completion:** `reconstructPar -time '0.05,0.1,...,2.0'` for ParaView.

## Key parameters (unchanged from prior setup)

| Parameter | Value |
|---|---|
| Domain | 0.04 × 0.04 × 0.24 m |
| Bed | z=0→0.10, porosityF=0.40, Ywood=1, Ts=800 K |
| endTime | 2 s |
| writeInterval | 0.05 s |
| adjustTimeStep | yes, maxCo=1 |
| Chemistry | wood → 0.30 char + 0.70 targas, Arrhenius (2.0e5 8000 300 -75000 1) |
| MPI | 2 ranks (mpirun -n 2 yade MPI_lambda.py) |
| CHAR_CORE_RADIUS | 0.0025 m (50% of r=0.005) |

## Known pitfalls

- **Terminal disconnect kills `tee` pipeline** — always use `nohup`
- **Don't forget to clean processor dirs** — decomposePar fails if they exist
- **Build needs KVM host** — container image lacks Yade coupling headers
- **lambdaDot=0 in text log is normal** — it's a volScalarField, not logged.
  Check `ls processor0/*/lambdaDot` instead.
- **Sphere solid fraction ≠ continuum porosityF** — spheres produce Us, not
  porosity. The 52% sphere fraction is fine with porosityF=0.40.

## Relevant files

- `tutorials/cases/posterDemo/MPI_lambda.py` — sphere packing, coupling, shrink, VTK
- `tutorials/cases/posterDemo/controlDict` — endTime=2, writeInterval=0.05
- `tutorials/cases/posterDemo/setFieldsDict` — bed region, initial fields
- `tutorials/cases/posterDemo/chemistryProperties` — wood pyrolysis reaction
- `/plans/trymarz/active/posterDemo-dense-packing.md` — full plan
