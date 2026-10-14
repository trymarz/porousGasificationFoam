# Contributing

*New contributors: start here.*

## Branch Naming

Branches use a short prefix that signals intent, followed by a kebab-case descriptive name. Five prefixes cover everything:

| Prefix | Used for |
|---|---|
| `feature/` | New user-facing capability — a new solver field, model, or tutorial exercising real functionality. |
| `fix/` | Observable bug becomes right. |
| `refactor/` | Code restructure intended not to change behaviour. Flag for careful review. |
| `docs/` | Documentation only (README, AGENTS.md, comments). |
| `chore/` | Everything else meta — formatter configs, build/Make tweaks, `.gitignore`, dependency bumps, tooling. |

Examples: `feature/UsInterp-laplace-smoothing`, `fix/regression-allrun-set-u`, `chore/format-and-docs`.

A single branch may bundle multiple low-risk meta concerns (e.g. formatter, docs, and dev-workflow changes can ride on one `chore/...` branch). Anything that can affect numerical results stays on its own branch.

## Merge Strategy

The repository uses **squash merge** — each PR becomes one commit on `main`, whose message is the PR title followed by the PR body. This keeps `main` linear and easy to scan with `git log --oneline`, while preserving the *why* and verification context inside `git log` / `git show`.

Because the squash commit is permanent and per-branch commits are not, the convention below applies to **PR titles and bodies**, not to individual commits on your working branch. Commit however you like locally; the squash collapses it.

## PR Titles

```
<type>[(<scope>)]: <imperative subject>
```

- `<type>` is one of `feat`, `fix`, `refactor`, `docs`, `chore` — the same vocabulary as branch prefixes (`feat` is the short form of `feature`).
- `<scope>` is optional. Use it when it adds clarity (e.g. `fix(regression): ...` vs an unrelated bug fix). Plausible scopes in this repo: `regression`, `DEM`, `solver`, `tutorials`, `README`, `AGENTS`, `pyrolysis`, `chemistry`.
- Subject is in imperative mood, lowercase first letter after the colon, ≤72 characters, no trailing period.

Examples:

```
feat(DEM): add UsInterp Laplace smoothing for solid velocity
fix(regression): keep Allrun/Allclean working under set -u
docs(README): clarify default chemistry
chore: apply clang-format across solver
```

## PR Body

Opening a PR auto-populates the description from `.github/pull_request_template.md`. Three sections:

- **Summary** — one or two sentences on what the PR changes. Always fill this in.
- **Why** — motivation: symptom, missing capability, or bug. Skip if the diff is obviously self-justifying (typo fix, formatter run).
- **Verification** — what you actually ran or checked: build target, tutorial cases, regression suite, residual sanity. Skip if the change cannot affect runtime (README only).

Aim for terse and specific. The body becomes part of `main`'s history once squash-merged, so future-you (and `git log`) benefit from precision now.

## Documentation

Source code is the source of truth for physics, equations, units, and algorithm choices. Comments live next to the implementation they describe — OpenFOAM-style file banner with a `Description` block, `//-` briefs above function declarations, and `// ...` narrative inside function bodies for non-obvious steps.

[doc/theory.md](doc/theory.md) summarises the model: per-step flow, a model → input → source table, and short-form equations. It does **not** restate implementation detail, which belongs in the source file where it cannot drift unnoticed during refactors. If theory.md and the code differ, the code wins.

When you submit a PR:

- **Behaviour change** → update the relevant code-comment block in the same commit.
- **Loop structure or step ordering change** → update the per-step flow in `doc/theory.md` too.
- **Equation change** → update the short-form equation in `doc/theory.md` too.
- **New documentation insight** → if it's about *what the code does*, write it as a code comment. If it's about *how to use* the solver or *how to navigate* the codebase, it belongs in the README or `doc/`.

If you catch a theory.md claim (beyond a short-form equation) that's already in the code as a comment, replace it with a pointer in the same PR. The migration is opportunistic — no need to wait for a dedicated cleanup branch.
