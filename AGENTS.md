# AGENTS.md

**For AI coding agents working on porousGasificationFoam.** Humans should read `README.md`; it is the user-facing source of truth. This file is a thin agent-operating layer on top of it — not a replacement.

## Staleness check (read this first)

This file may lag the codebase. Before acting on any specific claim below, sanity-check it against the current state of the files it references. If you find a contradiction — a path that no longer exists, a convention the recent code doesn't follow, a tool or flag that has been renamed — **stop and warn the user before proceeding**. Do not silently route around it.

Also flag if the *Last verified* date below predates significant recent commits in the affected area; in that case, propose a refresh rather than acting on stale guidance.

*Last verified: 2026-11-26*

## Documentation split

- **`README.md`** (short front page), **`doc/*.md`** and **`CONTRIBUTING.md`** — human-facing. Quick start in the README; every page in `doc/README.md`, in the six page groups of `CONTRIBUTING.md` → Documentation → Page groups; contribution workflow in `CONTRIBUTING.md`.
- **`AGENTS.md`** (this file) — agent-operating instructions only.

**Rule for new content**: if a fact is useful to *either* a human contributor *or* an agent, it goes in `README.md` or `doc/` (whichever fits; the README stays short and links out). Only put content in `AGENTS.md` when it is 100% agent-targeted and would be noise for a human reader (e.g. "pass git identity per-invocation rather than touching global config", "verify before claiming"). When in doubt, choose `README.md` — false positives there are cheap; missing facts are not.

For the current state of work in the repository — what's in flight, what's stable — use `git status`, `git log`, and `gh pr list` rather than expecting a written summary here. Per-contributor working state does not live in this file.

## Documentation source of truth

The rules are in `CONTRIBUTING.md` → Documentation: the code wins, each fact has one home, page groups, rendering, voice. This section only adds how an agent applies them.

- **Classify the fact before writing it.** What the code does exactly (an equation term, a key and its default, a `pgf` flag, a stage order) goes in a source comment or `pgf help`; a doc page restates it only with a pointer to that source. Why, assumptions, how-to and how the pieces fit go in `doc/` or the README.
- **Find the existing home first.** Grep `doc/`, `README.md` and the source for the fact before adding it; link to the home instead of writing a second copy.
- **Docs and code disagree → the code wins.** Act on the code, and fix the doc if it is in scope; otherwise tell the user which page is wrong. Never "correct" code to match a doc.
- **Answering a physics or behaviour question:** quote the source comment or the code, then the doc page that explains the idea. Do not re-derive from the docs alone.
- **"Document the physics"** means: the model idea and assumptions on the matching model page; the equation as implemented in the source comment. Ask if it is unclear which one the user wants.
- **A comment wants more than 3 lines** (`CONTRIBUTING.md` → Source comments): extract a named function or collaborator instead of writing more prose. Do not trim a banner `Description` by counting lines; trim only what restates the briefs below it.
- **Editing math or figures:** keep the colour-coded equations and SVG figures as `CONTRIBUTING.md` → Rendering describes.

## Git / VCS rules

- **Do not modify `git config --global`.** Pass identity per-invocation when needed: `git -c user.name="…" -c user.email="…" commit -m "…"`.
- **Never add a `Co-Authored-By:` footer naming the agent (or any other AI).** Commits must be attributed to the human user only. The user's identity (passed via `-c user.name` / `user.email` or already set in `git config`) is the sole author of every commit.
- Match the commit-message style of recent log entries (`git log --oneline -10`). The repo currently mixes Conventional-Commits-style prefixes (`docs:`, `fix:`) with short imperative subjects; pick the style of the area you are touching.
- **Never** use `--no-verify`, `--force-push`, or skip signing unless the user explicitly asks. If a pre-commit hook fails, fix the underlying issue and create a *new* commit — do not `--amend` around it.
- Stage specific files by name (`git add path/to/file`). Avoid `git add -A` / `git add .` — tutorial runs leave behind `processor*/`, `postProcessing/`, time directories, and logs that are not meant for version control.
- Do not push to `origin` unless the user explicitly asks.

## GitHub Issues Workflow

GitHub Issues + Project #4 ("PGF development") is the planning record for this
repo. Each issue is public, like the repo, and should show an accurate
Problem/Objective/Deliverables and rough progress. Detail goes in the PRs
that resolve it and in issue comments. The project board is currently private
to the maintainer, unlike the repo and its issues. When writing for an external
audience, don't imply board access; cite the issue by number, label and state.

**On creating a plan for this repo**: create an issue from the `Plan` template
(`.github/ISSUE_TEMPLATE/plan.md`). The title is `Plan: <title>`. Fill in:
- Problem: why the work is needed.
- Objective / Deliverables: what done looks like, plus acceptance criteria.
- A `Plan` checklist with one box per deliverable, each with a rough time
  estimate.

Then apply fitting labels.

**As each block lands**: tick that block's checkbox on the issue. This is the
routine, cheap touchpoint — a checkbox flip, not a rewrite. Nothing else on
the issue should change for routine progress.

**Body edits are explicit and rare**: rewrite Problem/Objective/Deliverables
only when the work proves the original spec wrong. Flag it to the user first,
then correct it deliberately. Never edit the spec as a side effect of routine
progress; progress notes go in PRs or issue comments.

**Before starting work tied to an issue number**: fetch the issue's current
body + recent comments (`gh issue view <N> --comments`). If it diverges from
what you were told (the user edited the issue directly), reconcile it with the
user; never silently overwrite it.

**Branch / PR linkage**: create the branch that resolves an issue from the
issue's own **Development** panel → "Create a branch", so GitHub links it
automatically; don't link it by hand. PR bodies include `Closes #<issue>`, so
merging auto-closes the issue. The project's "item closed → Done" workflow
rule then moves the card to `Done` automatically, with no manual board edit.

## Code navigation (Serena)

For C++ definitions, callers, subclasses and a file's structure, use the Serena tools first (`find_symbol`,
`find_referencing_symbols`, `find_implementations`, `get_symbols_overview`); read only the bodies you need.
Fall back to Grep/Read when they return nothing.

Use Grep for strings, dictionary keys, run-time selection names (`TypeName`, `addToRunTimeSelectionTable`),
macros and non-C++ files. Serena cannot see these, nor code under `#ifdef WITH_YADE` (normal-variant index).

Name paths: a method defined in a `.C` is `Class::method`; a leading `/Foam/...` separates a class from its
constructors; `get_symbols_overview` on a `.C` needs `depth=3`. Hook lines starting `gen-compile-commands:`
(session start, after edits) report the database and `lnInclude/`; act on a WARNING before trusting results.
After adding, renaming or deleting sources with Bash (`git mv`, `git rm`), run `utilities/gen-compile-commands.py --if-stale`.

## Verification habits

- **Before claiming a file, symbol, or flag exists, verify it.** Grep or read the file. Memory and prior conversation context are not authoritative; the current tree is.
- **Before suggesting a change in behaviour, locate the code that implements it.** A correction based on what you *think* the code does will be wrong often enough that the cost of checking pays for itself.
- **Before modifying a tutorial case, check whether it is listed in `applications/test/regression/cases.list` or `cases_yade.list` (the Tested column of `doc/tutorials.md` shows the tier).** Numerical changes will invalidate the committed baseline. Surface this to the user before proceeding so the baseline can be regenerated intentionally.

## Plans and interactive testing

- Substantial work is tracked in its GitHub issue (see "GitHub Issues Workflow"); read the issue and its comments before acting.
- For host-run OpenFOAM campaigns, guide the user through bounded checkpoints rather than silently running the campaign. Explain the purpose and expected evidence before each checkpoint, have the user run it, then interpret the output against the current source and plan.
- Keep disposable case output, logs, state directories, and post-processing data outside the checkout, normally below a user-selected test root such as `$PGF_TEST_ROOT`.
- Distinguish build success, solver completion, numerical comparison, and physical or diagnostic correctness. Do not call a test green from a tally alone; inspect per-case outcomes, relevant log diagnostics, and the actual fields or metrics under test.

## When to act vs ask

- **False-positive rule.** When in doubt whether something is in scope or what the user prefers, surface it. False questions are cheap; unsanctioned changes are expensive.
- **Do not expand scope.** No incidental refactors, no "while I'm here" cleanups, no new abstractions for hypothetical future needs. Bug fixes do not need surrounding cleanup.
- **Be explicit about what kind of verification you did.** This is a numerical solver: "type-checks and lints pass" is not the same as "the run produces correct results", and a successful build is not a successful regression. State what was actually exercised — not what the change *should* do, but what was observed.

## Pointers into the docs

When you need a load-bearing fact, go to `README.md` and `doc/` rather than re-deriving it from the code:

| Need | Section |
|---|---|
| Per-time-step flow / which source file implements each model and step | `doc/code-map.md` (follow its pointers into source comments — the code wins); short-form equations in `doc/theory.md` |
| Branch naming, PR titles, PR body, doc-strategy rule | `CONTRIBUTING.md` |
| Per-case input dictionaries (`chemistryProperties`, `solidThermophysicalProperties`, `radiationProperties`, `pyrolysisProperties`, `lambdaDict`, `heatTransferProperties`, `specieTransferProperties`, `porosityProperties`) | `doc/case-reference.md` (each section names the source that reads it) |
| DEM `lambdaDict` keys | `doc/case-reference.md` → "`constant/lambdaDict`" |
| Which side owns `λ` and what PGF sends back (`λ̇` or `r`) | `doc/theory-DEM.md` → "The idea"; the exchange itself in `doc/dem-exchange.md` |
| Required `0/` fields and their meaning | `doc/case-reference.md` → "Required initial fields" |
| Practical pitfalls (true vs bulk density, JANAF substitution, radiation calibration, time-step coupling, numerics symptoms) | `doc/case-guide.md` → "Tips for preparing new simulations" |
| Tutorial cases, and which test tier runs each | `doc/tutorials.md` (also parsed by `pgf copy --list`) |
| Every `pgf` command, flag and setting | `pgf help`, or the generated `doc/pgf-reference.md` |
| Build targets and dependencies | `doc/build.md` |
| clangd, Serena, Doxygen | `doc/dev-tools.md` |
| Regression framework — hook a case, capture a baseline, run comparisons | `doc/regression-testing.md` |
| GitHub Issues workflow — plan issues, checkbox ticks, reconciliation, branch/PR linkage | "GitHub Issues Workflow" (this file) |
