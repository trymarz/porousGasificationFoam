# .plans INDEX

> One-line status of every plan in this directory.

*Last updated: 2026-06-15 (night — updraftDemo Bug #1 (chemistry SIGFPE) fixed & host-verified; Bug #2 (solid-advection blowup) root-caused, fix designed, ready to implement)*

## Active (being shaped or ready to execute)

- **grc2026-poster-v2.md** — GRC 2026 poster v2. **Phase 1 DONE** — updraftDemo case + poster scripts built on `showcase/poster-demo` (48 files, 4 commits). Continuum-validated in-container. **Phase 2: two host-run crashes at t≈0.04s, both diagnosed.** Bug #1 (chemistry SIGFPE) fixed & host-verified → `debug-updraftDemo-chemistry-SIGFPE.md`. Bug #2 (the real one: solid-advection blowup `Ywood`→1e12) root-caused, fix designed → `fix-updraftDemo-solid-advection-blowup.md`. DEM coupling alive (Us ~2.5 mm/s, physical). Deadline June 18.
- **fix-updraftDemo-solid-advection-blowup.md** — **Bug #2, THE REAL DISEASE. Root-caused & verified from host log; fix designed, ready to implement (fresh session).** `Ywood` explodes 1.0008→7.7e12 in one step with ZERO chemistry source (log line 8411) → corrupts chemistry/energy → SIGFPE in `volPyrolysis::heatTransfer()`. Cause: porosity advection overshoots >1 (no clamp) → `rhoLoc=max(rho*(1-por),SMALL)` floors → explicit Ys advection `dt/rhoLoc·div(flux)` amplifies by `rhoLoc_face/rhoLoc_cell ≈ 1e17` (Us is fine, ~2.5 mm/s, NOT a CFL issue). Fix: clamp porosity≤1 + mask Ys advection out of empty cells. Exact diffs + verification in the plan.
- **debug-updraftDemo-chemistry-SIGFPE.md** — **Bug #1, DONE & host-verified.** SIGFPE in `calculateSourceTerms` (line 1660): unclamped solid-T update `dTi=newhi/(newCp*solidRho)*dt` blew up for near-empty reacting cells; ODE `derivatives()` clamps the same ratio, 1660 didn't. Guard+clamp applied (working tree), fired correctly on host. Chemistry was a *victim* of Bug #2 → superseded by the solid-advection plan above. Archive once Bug #2 lands.
- **verify-yade-coupling.md** — Verify the PGF-YADE coupling works end-to-end. The 2-line fix (`setParticleAction` before `update`) is applied but NEVER tested with YADE. Step-by-step: build with `--yade` on host, run LEI coupled case, verify 14 checkpoints. Deadline June 18.

## Backlog (ideas)

- **solver-m4-teardown-heap-corruption.md** — `serial_m4` completes the solve then aborts at exit with glibc `malloc_consolidate(): unaligned fastbin chunk` (heap corruption). Solver/library bug, not tutorials. Repro + ASan/Valgrind plan inside. Filed 2026-06-15.

## Archive (completed)

- **regression-ts-metrics-measure-dead-cells-DONE-2026-06-15.md** — Dropped `regressionTs{Average,Max,Min}` from all charOnlyMove regressionFunctions. Recaptured baselines, serial passes 3/3. Committed `be6eb02a`.
- **pr-monitor-tui-DONE-2026-06-15.md** — PR #20 (feature/monitor-tui → main) opened, merged into main.
- **test-foamcli-visual-on-host-2026-06-15.md** — Manual TUI test checklist passed (T1–T7).
- **debug-pr19-charOnlyMove-regression-2026-06-15.md** — Diagnosed benign: PR #19 reorder is dead-cell artifact, not real instability.
- **rebase-two-branches-2026-06-15.md** — Rebased fix/parallel-solid-bc and feature/LEI-gasifier onto main.
- **verify-foamcli-monitor-configs-2026-06-15.md** — All 20 TOML files validated.

