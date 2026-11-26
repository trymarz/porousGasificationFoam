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

## Issue Tracking

Planning work has a matching GitHub Issue (public, like this repo) tracked on a project board named "PGF development" — that's the place to check current status (Backlog/Ready/In progress/In review/Done). Deeper working detail (files to touch, approach, step-by-step log) goes in the PRs that resolve the issue and in its comments. The board itself is currently private to the maintainer, so external contributors should rely on the issue's own labels/state rather than the board view. Start the branch that resolves an issue from that issue's own **Development** panel ("Create a branch") so GitHub links the two automatically, and include `Closes #<issue>` in the PR body so merging the PR closes the issue and moves its board card to `Done` on its own.

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

### The code wins

When a doc page and the code disagree, the code is right and the page gets fixed. This holds for every page, practical ones included. The code changes faster than the docs, and the docs must not mislead.

### Each fact has one home

Other pages link to that home instead of repeating the fact. Where the home is depends on the kind of fact:

- **What the code does exactly**: an equation term, a dictionary key and its default, a `pgf` flag, a build path, the order of stages. Its home is the code: source comments, `pgf help`, the dictionary readers. A doc page may restate such a fact only with a pointer to its source (a file, or `pgf help <topic>`), so a reader can check it. Restate less, point more.
- **What the code does not say**: why the model is built this way, its assumptions and what it leaves out on purpose, how to approach a task (set up a case, choose a model, debug a run), and how the pieces fit together. The docs own this.

### Page groups

Each group answers one question. Inside a group, pages run from overview to detail. The [README](README.md) quick start comes before all groups; development and contribution come last.

| Group | Question | Pages, overview → detail |
|---|---|---|
| Installing and building | How do I get PGF onto a machine? | [install](doc/install.md), [build](doc/build.md), [containers](doc/containers.md) |
| Running cases | How do I set up and run a case? | [case setup](doc/case-setup.md), [ParaView](doc/paraview.md) |
| The pgf tool | What can pgf do, and how does it work? | [pgf](doc/pgf.md) |
| The model | What physics does it solve? | [theory](doc/theory.md), [DEM coupling](doc/theory-DEM.md) |
| How the code implements it | Where does each piece live, and in what order does it run? | [solid step](doc/solid-step.md) |
| Developing PGF | How do I change it safely? | this page, [regression testing](doc/regression-testing.md), [regression ledger](doc/regression-ledger.md) |

Task pages hold workflow and advice; exact flags and keys point to `pgf help` and the code. Model pages give the physics, its assumptions and what is left out, with short-form equations and physics before code names. Code pages are mostly maps with pointers into the source.

A new page joins one group and the index in [doc/README.md](doc/README.md).

### Source comments

Comments follow OpenFOAM style: a `Description` block in the file banner, `//-` briefs above declarations, `// ...` narrative inside function bodies for non-obvious steps. A comment is at most 3 lines; a longer one signals code that wants a named function, not more prose. The banner `Description` is exempt, as the one-time design spec of the file, but it should not restate the briefs below it.

### Rendering

GitHub is the target. Use what it renders well: colour-coded equations (`\color{...}` in `$$` math, one colour per kind of term), SVG figures in `doc/img/`, mermaid diagrams. Links between pages are relative. Portability to a docs-site generator is not a goal.

### Voice

Short declarative sentences, one idea per paragraph, physics before code names.

### When you submit a PR

- **Behaviour change** → update the code comment in the same commit, and any doc restatement that points to it.
- **Loop structure or step ordering change** → update the per-step flow in `doc/theory.md`.
- **Equation change** → update the short-form equation in `doc/theory.md`.
- **New `pgf` flag or dictionary key** → document it in `pgf help` or the reader's code first; a doc page links to it or restates it with a pointer.
- **New insight** → if it is *what the code does*, write it in a code comment or `pgf help`. If it is *why*, *how to* or *how the pieces fit*, it belongs in `doc/` or the README.
- **Duplicate found** → reduce it to one home plus links in the same PR. No need to wait for a cleanup branch.

## Private companion repository

The maintainer keeps work not meant for publication (e.g. third-party gasifier campaigns, server runs) in a private repo added as a second remote, `private`.

- Private branches and issues live on `private`. Its `main` is public `main` plus private-only commits, synced by merging `origin/main` into it.
- To publish, branch from `origin/main`, cherry-pick (or `git checkout <commit> -- <paths>`) only the wanted changes, push to `origin` and open a public PR.
- `utilities/githooks/pre-push` refuses a push to the public repo when an outgoing commit touches a path listed in `utilities/githooks/private-paths` (top-level `private/`, `tutorials/cases/LEIgasifier*`, `LEIgeometry.local`); deleting such a path is allowed. It needs `core.hooksPath` set as in [doc/build.md](doc/build.md).

## Related pages

- [Regression testing](doc/regression-testing.md): running the tests, adding a case.
- [Theory](doc/theory.md): the model, its equations and where they live in the source.
- [All documentation](doc/README.md)
