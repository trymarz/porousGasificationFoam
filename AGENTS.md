# AGENTS.md

**For AI coding agents working on porousGasificationFoam.** Humans should read `README.md`; it is the user-facing source of truth. This file is a thin agent-operating layer on top of it — not a replacement.

## Staleness check (read this first)

This file may lag the codebase. Before acting on any specific claim below, sanity-check it against the current state of the files it references. If you find a contradiction — a path that no longer exists, a convention the recent code doesn't follow, a tool or flag that has been renamed — **stop and warn the user before proceeding**. Do not silently route around it.

Also flag if the *Last verified* date below predates significant recent commits in the affected area; in that case, propose a refresh rather than acting on stale guidance.

*Last verified: 2026-06-16*

## Documentation split

- **`README.md`** (root + any subordinate READMEs) — human-facing. Quick start, build, input file reference, physical-model walkthrough, tutorial guide, troubleshooting, contribution-relevant invariants and gotchas.
- **`AGENTS.md`** (this file) — agent-operating instructions only.

**Rule for new content**: if a fact is useful to *either* a human contributor *or* an agent, it goes in `README.md`. Only put content in `AGENTS.md` when it is 100% agent-targeted and would be noise for a human reader (e.g. "pass git identity per-invocation rather than touching global config", "verify before claiming"). When in doubt, choose `README.md` — false positives there are cheap; missing facts are not.

For the current state of work in the repository — what's in flight, what's stable — use `git status`, `git log`, and `gh pr list` rather than expecting a written summary here. Per-contributor working state does not live in this file.

## Documentation source of truth

Physics, equations, units, and algorithm choices belong in source-code comments — OpenFOAM-style `Description` block in the file banner, `//-` briefs above function declarations, `// ...` narrative inside the implementing function. README Part II is a *tour* that points into the code; it does **not** restate equations or implementation detail.

In practice this means:

- When a user asks to "document the physics" of a piece of code, write the comment in the source file (or expand an existing block). Update Part II only if the high-level flow changes.
- When reading code to answer a physics question, quote the relevant comment block rather than re-deriving from the README. The code (and its comments) is authoritative.
- If you see Part II restating something the code already comments on, replace it with a pointer to the source location — don't keep the duplication "just in case".
- The migration from README-as-source-of-truth to code-as-source-of-truth is opportunistic: add comments to files as you touch them on other work, not in dedicated cleanup PRs.

The full human-facing version of this rule is in README → Development Workflow → Documentation.

## Git / VCS rules

- **Do not modify `git config --global`.** Pass identity per-invocation when needed: `git -c user.name="…" -c user.email="…" commit -m "…"`.
- **Never add a `Co-Authored-By:` footer naming the agent (or any other AI).** Commits must be attributed to the human user only. The user's identity (passed via `-c user.name` / `user.email` or already set in `git config`) is the sole author of every commit.
- Match the commit-message style of recent log entries (`git log --oneline -10`). The repo currently mixes Conventional-Commits-style prefixes (`docs:`, `fix:`) with short imperative subjects; pick the style of the area you are touching.
- **Never** use `--no-verify`, `--force-push`, or skip signing unless the user explicitly asks. If a pre-commit hook fails, fix the underlying issue and create a *new* commit — do not `--amend` around it.
- Stage specific files by name (`git add path/to/file`). Avoid `git add -A` / `git add .` — tutorial runs leave behind `processor*/`, `postProcessing/`, time directories, and logs that are not meant for version control.
- Do not push to `origin` unless the user explicitly asks.

## Verification habits

- **Before claiming a file, symbol, or flag exists, verify it.** Grep or read the file. Memory and prior conversation context are not authoritative; the current tree is.
- **Before suggesting a change in behaviour, locate the code that implements it.** A correction based on what you *think* the code does will be wrong often enough that the cost of checking pays for itself.
- **Before modifying a tutorial case, check whether it is listed in `applications/test/regression/cases.list`.** Numerical changes will invalidate the committed baseline. Surface this to the user before proceeding so the baseline can be regenerated intentionally.

## When to act vs ask

- **Falls-positive rule.** When in doubt whether something is in scope or what the user prefers, surface it. False questions are cheap; unsanctioned changes are expensive.
- **Do not expand scope.** No incidental refactors, no "while I'm here" cleanups, no new abstractions for hypothetical future needs. Bug fixes do not need surrounding cleanup.
- **Be explicit about what kind of verification you did.** This is a numerical solver: "type-checks and lints pass" is not the same as "the run produces correct results", and a successful build is not a successful regression. State what was actually exercised — not what the change *should* do, but what was observed.

## DEM role in PGF coupling (frequent point of confusion)

YADE DEM spheres **do not represent porosity**. Porosity is carried by the PGF continuum field `porosityF` and is entirely independent of the sphere positions/sizes. The spheres' sole role is:

1. **Create a smooth `Us` (solid velocity) field** — DEM sphere motion is mapped to the grid as `UsDEM`, smoothed to `Us`, which advects the solid-phase PGF fields (solid fraction, solid enthalpy, solid species).
2. **Provide lambdaDot** — the thermal shrink signal from the PGF gas phase is pushed onto each sphere via FoamCoupling; `changeRadius()` uses it to shrink spheres (visual/geometrical), but this does NOT drive the porosity field.

**Consequences for setup:**
- Spheres should be packed **densely enough to produce a smooth, jump-free `Us` field** across the bed region. Sparse spheres (current r=0.003, 11% solid fraction) leave gaps → Us jumps discontinuously where spheres pass → poor advection of solid-phase fields, and spheres fall through without interlocking.
- The sphere packing density does NOT need to match `porosityF`. The bed `porosityF=0.40` (60% solid) in `setFieldsDict` is independent of how many spheres are placed. You can have `porosityF=0.40` with 11% solid fraction from spheres — but the Us field will be poor.
- The goal is **dense interlocking spheres** (e.g. r=0.0045, 120–160 spheres in a 4×4×8 grid) so the bed self-supports under gravity and Us varies smoothly cell-to-cell.
- `CHAR_CORE_RADIUS` (the minimum sphere radius after shrinkage) should be set so spheres stay large enough to maintain grid contact even after full shrinkage — otherwise they lose coupling to the Eulerian mesh.

**What NOT to do:**
- Do NOT try to make the sphere packing fraction match `porosityF`. That is a common mistake — the spheres represent moving discrete objects that produce a continuum velocity field, not the solid volume fraction itself.
- Do NOT assume shrinking spheres change the porosity — `porosityF` evolves via its own transport equation with `Us`, not via sphere geometry.

## Pointers into README

When you need a load-bearing fact, go to the README rather than re-deriving it from the code:

| Need | Section |
|---|---|
| Per-time-step flow / which source file implements each step | "Part II — Physics and Implementation → Per-Time-Step Tour" (for equations and details, follow the tour's pointers into source comments — not the README) |
| Branch naming, PR titles, PR body, doc-strategy rule | "Development Workflow" |
| Per-case input dictionaries (`chemistryProperties`, `solidThermophysicalProperties`, `radiationProperties`, `pyrolysisProperties`, `heatTransferProperties`, `specieTransferProperties`, `porosityProperties`) | "Input File Reference" |
| Required `0/` fields and their meaning | "Required Initial Fields" |
| Practical pitfalls (true vs bulk density, JANAF substitution, radiation calibration, time-step coupling) | "Tips for Preparing New Simulations" |
| Build targets and dependencies | "Build System" |
| Common failure modes | "Troubleshooting" |
| Regression framework — hook a case, capture a baseline, run comparisons | "Regression Testing" |
