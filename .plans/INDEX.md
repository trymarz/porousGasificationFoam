# .plans INDEX

> One-line status of every plan in this directory.

*Last updated: 2026-06-15 (late evening — post-Claude verification)*

## Active (being shaped or ready to execute)

- **grc2026-poster-v2.md** — GRC 2026 poster v2. **Phase 1 DONE** — updraftDemo case + poster scripts built on `showcase/poster-demo` (48 files, 4 commits). Continuum-validated in-container. **Phase 2: host run crashed** — SIGFPE in solid chemistry ODE at t≈0.04s (both seulex and RKCK45). DEM coupling alive. Debug plan → `debug-updraftDemo-chemistry-SIGFPE.md`. Deadline June 18.
- **debug-updraftDemo-chemistry-SIGFPE.md** — Debug SIGFPE in `calculateSourceTerms` at t=0.04s. Plan: isolate chemistry-off, add crash diagnostics, identify root cause. **Ready: hand to Claude for implementation.**
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

