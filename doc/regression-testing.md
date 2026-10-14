# Regression testing

Numerical regression tests live under `applications/test/regression/`. The framework runs selected tutorial cases, extracts a small set of summary scalars produced by OpenFOAM `volFieldValue` function objects, and diffs the results against a committed reference baseline within a numerical tolerance.

The point is to give refactors a safety net: any change that perturbs the selected scalars beyond tolerance fails the regression and surfaces a clear diff. The gate is deliberately **honest** — it never reports success unless it actually ran a case and compared it (see the outcome contract below).

The framework has two runners that share one outcome vocabulary:

- **Serial suite** — `Allrun` / `Allclean`, `cases.list`, `tools/runCase.sh`, `tools/compareScalars.py`.
- **DEM (Yade) suite** — `Allrun.yade`, `cases_yade.list`, `tools/runDEMCase.sh` (requires a Yade-enabled build; see below).
- **Shared logic** — `tools/regressionLib.sh` holds the pieces both runners must agree on: how a case list is loaded, how a case's exit status becomes an outcome, how the summary decides whether the suite is green, and the bounded-concurrency mechanics. `tools/regressionState.py` serialises run state as JSON (standard library only, no project imports).

Per-case bits live in `tutorials/cases/<caseName>/system/regressionFunctions` (`volFieldValue` blocks included from `controlDict`) and `tutorials/cases/<caseName>/reference/postProcessing/` (committed baseline).

**The suite scripts are self-contained.** They need bash, `python3` and the OpenFOAM environment, and nothing else — no external tool takes part in deciding which cases run, how they are cleaned, what a case's `Allrun` does, how output is compared, or whether the suite is green. Tooling that wants to watch a run reads the optional structured state described below; it is a reader, never a participant. If a helper ever appears to be *required* to run regressions, that is a bug in the helper.

### Outcome contract

Every case ends in exactly one outcome, keyed off the case's exit status (standard shell conventions), and both the printed summary and the suite exit code reflect it:

| Exit code | Outcome | Green? | Meaning |
|---|---|---|---|
| `0` | `PASS` | yes | ran, compared, within tolerance |
| `1` | `FAIL` | no | ran, compared, **diverged** beyond tolerance |
| `2` | `ERROR` | no | infrastructure: missing case dir, missing baseline, dirty slate, no `postProcessing/`, comparator missing |
| `124` | `TIMEOUT` | no | the run exceeded its timeout and its process group was terminated |
| `128+N` | `CRASH (SIG…)` | no | the run died on signal N (e.g. `134` → `CRASH (SIGABRT)`, `139` → `CRASH (SIGSEGV)`) |

**The only green suite is one where at least one case ran and every case PASSed.** There is no benign "skip": a case with no reference baseline is an `ERROR`, not a silently-passing skip; an empty or unreadable `cases.list` / `cases_yade.list` is an `ERROR`; a `--tag` or `--case` filter that matched no case is an `ERROR` (exit 2), not an empty green run. A suite that compared nothing can never report success. Skip a suite by not invoking its driver, never by emptying its inventory.

### Tiers

`cases.list` tags each case:

- **`fast`** — small fixtures, run by default (`./Allrun`) and intended as the pre-push gate.
- **`convergence`** — a mesh-refinement study; run manually with `--tag convergence` or `--tag all`.

The DEM cases are inventoried separately in `cases_yade.list` and run only by `Allrun.yade`.

### Hooking a case into regression

Two steps per case:

1. Drop a `system/regressionFunctions` file describing the metrics to extract (see `tutorials/cases/charOnlyMoveCases/serial/system/regressionFunctions` for the pilot example). Add `#includeIfPresent "regressionFunctions"` at the bottom of `system/controlDict` inside a `functions { ... }` block.
2. Add the case path (and optional tag) to `applications/test/regression/cases.list`.

Then capture a baseline (see below). Because a missing baseline is now an `ERROR`, a newly-registered case fails the suite until its baseline is committed — register and baseline together.

### Capturing the baseline

Run the case with the OpenFOAM toolchain available and commit the resulting `postProcessing/` under `reference/`:

```bash
cd tutorials/cases/charOnlyMoveCases/serial
./Allclean
./Allrun
mkdir -p reference
cp -r postProcessing reference/
git add reference
git commit -m "Capture charOnlyMoveCases/serial regression baseline"
```

After commit, every subsequent run of `applications/test/regression/Allrun` compares fresh output against `reference/postProcessing/` and reports the outcome.

### Running regressions

```bash
cd applications/test/regression
./Allrun                                  # fast tier (default), one case at a time
./Allrun --tag all                        # every case regardless of tag
./Allrun --case charOnlyMoveCases/serial  # a single case
./Allrun --no-run                         # compare existing output only
./Allrun --rtol 1e-3                      # override default relative tolerance
./Allrun --jobs 4                         # four cases at a time (see below)
./Allrun --list                           # show the selected cases, run nothing
./Allrun --state-dir /scratch/reg-state   # also write machine-readable run state
```

Exit code `0` means every case that ran PASSed; any non-zero means at least one case was not green (FAIL/ERROR/TIMEOUT/CRASH). The summary lists each case with its outcome and, on a FAIL, the rows that diverged; a CRASH shows the signal name so a teardown abort is not mistaken for a numerical divergence.

The solver must be on `PATH` (source the OpenFOAM environment and build the solver first). A run tier that cannot find the solver produces `ERROR`/`CRASH` outcomes, not a false green.

The per-case runners are supported entry points in their own right, for when you want one case without the suite around it:

```bash
tools/runCase.sh    tutorials/cases/canonical/pyrolysis [--no-run] [--rtol R] [--atol A]
tools/runDEMCase.sh tutorials/cases/simple_line_case [--timeout S] [--nsteps N]
```

Both return the same taxonomy as the suite, and both accept the optional state flags described below.

### Suite concurrency (`--jobs`) is not case decomposition

`--jobs N` is the number of regression **cases** the driver may have in flight at once. It says nothing about MPI: it does not become `-np N`, it does not touch `decomposeParDict`, and it does not change how any case invokes its solver.

Whether a case runs serially, calls `decomposePar`, uses `runParallel`, launches `mpirun` itself, or starts a Yade coupling process is part of the solution procedure being tested, and lives in that case's own `Allrun`. Nothing in the regression framework second-guesses it. Budget cores accordingly: `--jobs 4` over four 2-rank cases wants eight cores.

`--jobs 1` is exactly the default sequential run — same case order, same outcomes, same output. Concurrency changes only three things:

- each case's output is captured to a file instead of streaming live, replayed for failing cases after the run so the diagnostics stay readable and in `cases.list` order;
- a one-line verdict is printed as each case finishes;
- with no `--state-dir`, a temporary directory is created for that capture and removed on exit.

A malformed `--jobs` value (zero, negative, non-numeric) is an `ERROR`/exit 2 rather than a silently coerced default.

### Machine-readable listing and run state

Both drivers can describe a selection without touching it, and can record a run as it happens. Both are optional: with neither flag the drivers behave exactly as they always have and write nothing extra.

**Listing.** `--list` prints the selected cases; adding `--json` emits the manifest an external observer needs in order to know what a run of the *same arguments* will do — the filters are applied by one code path, so a listing cannot disagree with the run:

```bash
./Allrun --list --json --tag fast
./Allrun.yade --list --json
```

```json
{
  "schema_version": 1, "record": "listing",
  "suite": "regression", "driver": "Allrun",
  "project_root": "/path/to/pgf",
  "cases": [
    { "id": "tutorials/cases/canonical/pyrolysis", "name": "pyrolysis",
      "path": "/path/to/pgf/tutorials/cases/canonical/pyrolysis",
      "runner": "runCase.sh", "tag": "fast" }
  ]
}
```

A case `id` is its `cases.list`-relative path, not its basename, so two cases with the same basename can never be confused. (`cases.list` is whitespace-delimited, so a case path cannot contain spaces — a constraint of the inventory file, not of the protocol.) Listing a missing, empty or unmatched selection exits 2.

**Run state.** `--state-dir DIR` makes the driver record what it is doing, under a directory the caller owns — deliberately outside the cases, so a case's `Allclean` cannot delete the record of the run in progress:

```text
<state-dir>/
  suite.json            selection + options for this run
  events.ndjson         append-only event stream (one JSON object per line)
  result.json           final suite result, written atomically
  cases/<case-id>/
    state.json           current phase
    events.ndjson        this case's events
    result.json          this case's terminal result
    stdout.log, stderr.log
    compare.log          comparator output (serial suite)
```

Events are `suite_started`, `case_queued`, `case_started`, `case_phase`, `case_finished`, `suite_finished`; phases are `queued`, `clean`, `run`, `assert`, `compare`. Each terminal result carries the status, the **raw exit code**, the signal number and name for a signal death, timestamps and elapsed time, the phase it failed in, a human-readable message, artifact paths, and the comparator's file counts.

Two invariants make the state trustworthy:

- **The exit code remains the contract.** JSON is an additional representation of the same outcome, never a replacement, and a failed state write never changes a case's result.
- **An absent result is never a pass.** If a runner dies before writing one, the driver synthesises the outcome from the wait status it actually observed; if a case's own `result.json` disagrees with that status, the observed status wins and the disagreement is recorded (`result_mismatch`), so a stale file from an earlier run cannot turn a crash green.

`foamcli test` is one consumer of this contract — a convenience wrapper that runs `Allrun` and renders its state as a dashboard or JSON. It is optional in both directions: PGF never calls it, and it never decides whether a PGF case passed.

### DEM (Yade) smoke suite

```bash
applications/test/regression/Allrun.yade --short   # fast smoke: YADE_NSTEPS=200
applications/test/regression/Allrun.yade           # full run (hours)
applications/test/regression/Allrun.yade --list    # selected DEM cases, run nothing
```

Requires a Yade-enabled solver build (`./build.sh --yade` / `build-pgf --yade`), Yade, and `mpirun`. `--short` caps each case to a few hundred coupling steps so it runs in seconds and checks the coupling handshake and output rather than a converged result; a `YADE_NSTEPS`/`YADE_TIMEOUT` preset in the environment overrides the `--short` defaults. Each case is validated for `RUN FINISH`, an active DEM coupling handshake, sphere/spring VTK output, and a positive PGF time step in `dtInfo.txt`.

The DEM driver takes the same `--list`/`--json`/`--state-dir` flags as the serial one, and the same `--jobs N` — but it defaults to **one** case at a time and warns when you raise it, because concurrent DEM execution has not been verified: each case spawns its own `mpirun` → Yade → `MPI_Comm_spawn`'d solver tree, so two at once oversubscribe the machine in a way a serial case does not.

A hung DEM run is bounded by `YADE_TIMEOUT` and terminated by signalling **only its own process group** (`setsid` + `timeout --kill-after`) — the whole tree of `mpirun` → `yade` → the `MPI_Comm_spawn`'d solver. The runner never issues a host-wide `pkill`, which would kill unrelated solver runs elsewhere on the machine. `cases_yade.list` is the DEM inventory (kept separate from the serial `cases.list`).

### Known caveat: teardown abort surfaces as CRASH

Some cases run to completion and write correct output, then the solver aborts during teardown (a heap-corruption abort after `End`, e.g. `malloc_consolidate(): invalid chunk size`). Because the process did not exit cleanly, the hardened runner reports this as `CRASH (SIGABRT)`, **not** `PASS` — even though the case's scalars match the baseline within tolerance. This is deliberate: the runner reports what the process did, and a solver that corrupts its heap on the way out is not a healthy pass.

This is a solver/library teardown lifetime defect, tracked separately from the regression tooling. It is **intermittent**: the same binary running the same `fast`-tier case has aborted on every attempt in one environment and completed cleanly on every attempt in another, and the same fault has also been seen to surface as a teardown *hang* rather than an abort. So a green fast tier is evidence about a particular run, not about the code, and a red one is not necessarily a regression introduced by the change under test.

A tracked pre-push hook running the fast tier is therefore deferred until the teardown defect is fixed — a gate that goes red for reasons outside a given change would only train people to bypass it. Do **not** paper over the abort with `|| true`; that would trade an honest CRASH for a false green.

### What the comparison does

`tools/compareScalars.py` walks `reference/postProcessing/<funcName>/<time>/` files and, for each, locates the matching freshly-produced file under `postProcessing/<funcName>/<time>/`. It compares row-by-row, column-by-column with the rule:

```
pass if  abs(a - b) <= atol + rtol * max(|a|, |b|)
```

Defaults: `rtol=1e-4`, `atol=1e-12`. Both can be overridden per-run. The header line of OpenFOAM `volFieldValue.dat` files is parsed, so column names appear in failure reports.

### Limitations

- Comparison is over scalar reductions extracted by function objects, not full field comparisons. A regression that perfectly cancels in every selected scalar will go undetected. Add more metrics to catch a wider class of bugs.
- Floating-point sensitivity to MPI decomposition is real: rerun on the same `numberOfSubdomains` as the baseline. The pilot case `charOnlyMoveCases/serial` runs **serial (1 proc)** by design — it is a small, fast fixture for the prescribed-`Us` solid-transport path. (The full `charOnlyMoveCases/parallel` tutorial is large and its 4-proc run currently hits a processor-boundary breakdown in the moving-solid immersed boundary, so it is not used as a regression baseline.) Because the fixture is serial, this regression does not exercise the parallel path.
- The reference baseline is only as correct as the run that produced it. When you change the model intentionally, regenerate the baseline and state why in the commit message.
