# Session Summary — 2026-06-16

## What we accomplished

### 1. DIAG probes → root cause NOT found, but bug did not reproduce
- Added DIAG probes to `porousGasificationFoam.C`, `lambdaDotModel.C`, `MPI_lambda.py` (Phase 3 of the debug plan)
- Ran updraftDemo case on host: **288+ timesteps, 1,671 DIAG lines, 0 corruption**
- nParticles[0] always 0 or 1 — the original bug (cell-0 overwrite to 320) never fired
- Original corruption likely a stale/broken solver binary — clean rebuild resolved it

### 2. DIAG probes removed + defensive bounds check added
- Stripped all DIAG `Info<<` and `print()` calls from 3 files
- Added `cellI >= mesh_.nCells()` guard at 3 sites in `lambdaDotModel.C`
- Commit: `5ea534d4` in PGF repo

### 3. updraftDemo long run — clean, 73% complete before stopped
- Ran to Time=14.5/20 (3.8 hours wall time), 0 errors
- Gas mass: 0.169 kg, Ywood consumed: 35%, T gas plateau at 800 K
- Postprocessing: 29 time directories, centerline probes, solid mass CSVs, mid-plane VTK slices
- **Key finding:** nParticles and porosityF are in DIFFERENT regions — particles at the bottom, chemistry burn zone higher up. This is correct for the updraftDemo case setup but not ideal for the poster.

### 4. posterDemo plan — designed the minimal coupling-demo case
- **4×4×8 cells** (128 total): 5 solid layers + 3 air layers
- **~120 spheres** packed in the bed (1-3 per solid cell)
- **Ts-driven shrinkage**: lambdaDot = f(Ts) instead of y-position function
- **Chemistry**: conduction-only Ts for first test (hot inlet at 800K)
- **Core idea**: spheres shrink from Ts → bed compacts → Us advects porosityF upward
- Plan at: `/plans/porousGasificationFoam/active/minimal-poster-demo-2026-06-16.md`

## Code changes (PGF repo)

| Commit | What |
|--------|------|
| `5ea534d4` | Remove DIAG probes, add cellI bounds check in lambdaDotModel.C |

## Plans closed

| Plan | Status | Why |
|------|--------|-----|
| `debug-nParticles-cell-zero-overwrite` | done | Corruption did not reproduce after rebuild; defensive guard added |
| `verify-yade-coupling` | done | All coupling indicators green: 0 SIGSEGV, nParticles correct, lambdaDot flowing |

## New plan

| Plan | Status | Where |
|------|--------|-------|
| `minimal-poster-demo-2026-06-16` | active | `/plans/…/active/` |

## State for next session

- Solver is clean (no DIAG spam, bounds check in place)
- updraftDemo output available for postprocessing (29 time dirs, probes, VTK)
- posterDemo plan ready to implement — needs code change (Ts-driven lambdaDot) + new case files
- **Handoff:** tell Claude "Implement .agent-plan.md" after launching with `agent-run.sh claude /workspace`

## Key unresolved

- The `updraftDemo` run was terminated partway (Time=14.5/20) — could restart from latestTime
- posterDemo needs to be built and run to verify the Ts-driven coupling works
- The user's core question: does sphere-shrinkage → bed-compaction → Us-advection actually produce visible porosityF transport?
