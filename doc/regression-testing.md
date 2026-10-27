# Regression testing

Refactor safety net: the framework runs selected tutorial cases, extracts summary scalars (OpenFOAM `volFieldValue` function objects) and diffs them against a committed baseline within a tolerance. It lives in `applications/test/regression/` and needs only bash, `python3` and the OpenFOAM environment.

## Run it

Source OpenFOAM and build the solver first (a missing solver gives `ERROR`/`CRASH`, never a false green).

```bash
cd applications/test/regression
./Allrun                                  # fast tier, one case at a time
./Allrun --tag all                        # every case (adds the convergence tier)
./Allrun --case charOnlyMoveCases/serial  # one case, whatever its tag
./Allrun --list                           # show the selection, run nothing
./Allrun --no-run                         # compare existing output only
./Allrun --rtol 1e-3                      # override relative tolerance
./Allrun --jobs 4                         # four cases at once
./Allrun --run-root /scratch/r --keep-runs  # where the case copies go; keep passed ones too
```

Exit `0` = every case passed; non-zero = at least one did not. A single case without the suite: `tools/runCase.sh tutorials/cases/canonical/pyrolysis [--no-run] [--run-dir D] [--rtol R] [--atol A]`. To run against an isolated build instead of the solver on your `PATH`, prefix the command with `./pgf-env --` (see [Isolated builds](build.md#isolated-builds-and-pgf-env)).

`--case` accepts the `cases.list` path, a trailing part of it or the basename, and selects every tag unless `--tag` is also given.

**Cases run from a copy.** Each case is copied to `<run root>/<case path with / as __>/<case path>` and cleaned, run and compared there against the source tree's `reference/`, so the source tree never gets logs, time directories, `processor*/` or `postProcessing/`. The copy keeps its symlinks, and every sibling directory on the way up (including `applications/`) is a symlink back into the source tree, so `../MicroTGA-DEM_lambdaDot_common/…` and `../../../applications/…` resolve as usual. The run root defaults to `${PGF_RUN_ROOT:-${TMPDIR:-/tmp}/pgf-runs}/regression-<time>-<pid>`. Copies of passed cases are deleted, failed ones kept (the runner prints `run copy <path>`), all of them with `--keep-runs`. `--in-place` runs in the source tree as before. With `--no-run`, the copy includes whatever output is already in the source case. The DEM driver (`Allrun.yade`) still runs in place.

## Outcomes

Each case ends in one outcome, taken from its exit status; the summary and suite exit code both reflect it.

| Exit code | Outcome | Meaning |
|---|---|---|
| `0` | `PASS` | ran, compared, within tolerance |
| `1` | `FAIL` | ran, compared, diverged beyond tolerance |
| `2` | `ERROR` | missing case dir or baseline, dirty slate, no `postProcessing/`, comparator missing |
| `124` | `TIMEOUT` | exceeded its timeout; process group terminated |
| `128+N` | `CRASH (SIG…)` | died on signal N (`134` = SIGABRT, `139` = SIGSEGV) |

The suite is green only if at least one case ran and every case passed. There is no skip: a case without a baseline, an empty or unreadable case list, or a `--tag`/`--case` filter matching nothing is an `ERROR` (exit 2). To skip a suite, don't invoke its driver.

**Tiers** (tagged in `cases.list`): `fast` runs by default and is meant as the pre-push gate; `convergence` is a mesh-refinement study, run with `--tag convergence` or `--tag all`. DEM cases are listed separately in `cases_yade.list`.

### Tiers with `pgf-test`

`pgf-test` runs a whole tier against this checkout's isolated build (see [Isolated builds](build.md#isolated-builds-and-pgf-env)) and remembers the result. The tiers are rows in `applications/test/tiers.conf`: driver, build variant, jobs, timeout, `pgf-env` flags, selection and cadence.

| Tier | Driver and selection | Variant | Jobs | Cadence |
|---|---|---|---|---|
| `fast` | `Allrun --tag fast` | normal | 4 | every push |
| `convergence` | `Allrun --tag convergence` | normal | 2 | nightly or `--if-stale 7d` |
| `dem` | `Allrun.yade` | `--yade` | 1 | before DEM-touching merges |

```bash
./pgf-test fast                        # build or reuse the prefix, run the tier, record it
./pgf-test convergence --if-stale 7d   # run only if the last PASS is for other sources or older than 7 days
./pgf-test all                         # every tier in order; exits with the first non-zero status
./pgf-test --status                    # per tier: last outcome, when, tested key, current or stale
./pgf-test --gc --keep-runs 10 --keep-prefixes 5 [--dry-run]
```

Each run goes to `$PGF_RUN_ROOT/pgf-test-<tier>-<time>-<pid>/` (`build.log`, `driver.log`, `state/` with `result.json`, and `cases/` with the kept case copies). `PGF_RUN_ROOT` defaults to `${TMPDIR:-/tmp}/pgf-runs`. The record `$PGF_RUN_ROOT/last/<tier>.json` holds the build key, source fingerprint, git `HEAD`, start and end time, outcome counts, the exit code and the paths to the run's state. A tier is **stale** when the key it last tested is not this tree's current key, or with `--if-stale AGE` when that run is older than `AGE`. `--if-stale` reruns a tier whose last run failed, even for the same key. `--rebuild` deletes the tier's prefix and builds it again. `--gc` keeps the newest N run dirs plus every dir a record points at, and the newest M prefixes. It never removes this tree's prefixes (either variant) or a prefix whose build lock is held. `PGF_TIERS_CONF` points `pgf-test` at another tiers file. Exit codes are the drivers' (see [Outcomes](#outcomes)).

## Add a case

1. Add `system/regressionFunctions` with the metrics to extract (example: `tutorials/cases/charOnlyMoveCases/serial/system/regressionFunctions`), and `#includeIfPresent "regressionFunctions"` inside `functions { … }` at the bottom of `system/controlDict`.
2. Add the case path (and optional tag) to `applications/test/regression/cases.list`.
3. Capture and commit a baseline (a case without one fails the suite, so register and baseline together). For a new case, run it in a copy and take its whole `postProcessing/`:

```bash
applications/test/regression/Allrun --case charOnlyMoveCases/serial --run-root /scratch/r --keep-runs
case=tutorials/cases/charOnlyMoveCases/serial
mkdir -p $case/reference && cp -r /scratch/r/${case//\//__}/$case/postProcessing $case/reference/
git add $case/reference && git commit -m "Capture charOnlyMoveCases/serial regression baseline"
```

The baseline is only as correct as the run that made it. After an intentional model change, recapture it from a kept run with `tools/captureBaseline.sh`, and say why in the commit message:

```bash
applications/test/regression/tools/captureBaseline.sh $case --run-dir /scratch/r/${case//\//__}/$case [--dry-run] [--all-files]
```

By default it copies only the reference files that fail the comparison, so files still within tolerance are not rewritten. `--all-files` copies every reference file, and `--dry-run` copies nothing. It never adds a file that is not already in `reference/`. For each file it prints the first, middle and last sample before and after.

## How the comparison works

`tools/compareScalars.py` matches each file under `reference/postProcessing/<funcName>/<time>/` to the fresh one in `postProcessing/` and compares row by row, column by column:

```
pass if  abs(a - b) <= atol + rtol * max(|a|, |b|)
```

Defaults `rtol=1e-4`, `atol=1e-12`. Column names from the `volFieldValue.dat` header appear in failure reports.

## Limitations

- Only scalar reductions are compared, not full fields; a change that cancels in every selected scalar goes undetected. Add metrics to widen coverage.
- Results depend on MPI decomposition: rerun with the baseline's `numberOfSubdomains`. The pilot `charOnlyMoveCases/serial` is serial (1 proc) by design, so the suite does not exercise the parallel path. `charOnlyMoveCases/parallel` is not a baseline because its 4-proc run hits a processor-boundary breakdown in the moving-solid immersed boundary.

---

## Protocol reference

### Concurrency (`--jobs`) is not case decomposition

`--jobs N` is how many **cases** the driver runs at once. It does not become `-np N` and does not touch `decomposeParDict`. Whether a case runs serially, calls `decomposePar`, uses `runParallel`, launches `mpirun` or starts Yade is part of the procedure under test and lives in the case's own `Allrun`. Budget cores accordingly: `--jobs 4` over four 2-rank cases needs eight.

`--jobs 1` is the default sequential run, unchanged. With `N > 1`:
- each case's output goes to a file and is replayed for failing cases after the run, in `cases.list` order;
- a one-line verdict prints as each case finishes;
- without `--state-dir`, a temporary capture directory is created and removed on exit.

A zero, negative or non-numeric `--jobs` is an `ERROR` (exit 2).

### DEM (Yade) suite

```bash
applications/test/regression/Allrun.yade --short   # smoke: YADE_NSTEPS=200
applications/test/regression/Allrun.yade           # full run (hours)
applications/test/regression/Allrun.yade --list    # selection only
tools/runDEMCase.sh tutorials/cases/simple_line_case [--timeout S] [--nsteps N]
```

- Needs a Yade-enabled build (`./build.sh --yade` / `build-pgf --yade`), Yade and `mpirun`. Cases are listed in `cases_yade.list`.
- `--short` caps each case at a few hundred coupling steps (seconds): it checks the coupling handshake and output, not a converged result. `YADE_NSTEPS` / `YADE_TIMEOUT` in the environment override its defaults.
- Each case is checked for `RUN FINISH`, an active coupling handshake, sphere/spring VTK output and a positive PGF time step in `dtInfo.txt`.
- Takes the same `--list`, `--json`, `--state-dir` and `--jobs` flags, but defaults to **one** case at a time and warns if you raise it: concurrent DEM runs are unverified, and each spawns its own `mpirun` → Yade → `MPI_Comm_spawn`'d solver tree.
- A hung run is bounded by `YADE_TIMEOUT` and killed by signalling only its own process group (`setsid` + `timeout --kill-after`). There is no host-wide `pkill`, which would kill unrelated solver runs.

### Scripts

- Serial suite: `Allrun`/`Allclean`, `cases.list`, `tools/runCase.sh`, `tools/compareScalars.py`, `tools/captureBaseline.sh`. DEM suite: `Allrun.yade`, `cases_yade.list`, `tools/runDEMCase.sh`.
- `tools/regressionLib.sh` holds what both runners must agree on: loading the case list, mapping exit status to outcome, deciding if the suite is green, bounded concurrency, and copying a case into a run root (`reg_stage_case`). `tools/regressionState.py` writes run state as JSON (standard library only).
- Both runners return the same outcomes as the suite and accept the state flags below.

### Machine-readable listing and run state

Both are optional; without the flags the drivers write nothing extra.

**Listing.** `--list --json` prints what a run of the same arguments will do. The same code applies the filters, so a listing cannot disagree with the run. A missing, empty or unmatched selection exits 2.

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

A case `id` is its `cases.list`-relative path, not its basename, so same-named cases cannot be confused. `cases.list` is whitespace-delimited, so case paths cannot contain spaces.

**Run state.** `--state-dir DIR` records the run under a directory you own, outside the cases so a case's `Allclean` cannot delete it.

```text
<state-dir>/
  suite.json            selection + options for this run
  events.ndjson         append-only event stream, one JSON object per line
  result.json           final suite result, written atomically
  cases/<case-id>/
    state.json           current phase
    events.ndjson        this case's events
    result.json          this case's terminal result
    stdout.log, stderr.log
    compare.log          comparator output (serial suite)
```

- Events: `suite_started`, `case_queued`, `case_started`, `case_phase`, `case_finished`, `suite_finished`. Phases: `queued`, `clean`, `run`, `assert`, `compare`.
- A terminal result holds the status, **raw exit code**, signal number and name, timestamps, elapsed time, the phase it failed in, a message, artifact paths and comparator file counts.

Two invariants:
- **The exit code is the contract.** JSON is another view of the same outcome, and a failed state write never changes a result.
- **No result is never a pass.** If a runner dies before writing one, the driver builds the outcome from the wait status it observed. If a case's `result.json` disagrees with that status, the observed status wins and the mismatch is recorded (`result_mismatch`), so a stale file cannot turn a crash green.

`foamcli test` is one optional consumer: it runs `Allrun` and shows its state as a dashboard or JSON. PGF never calls it, and it never decides whether a case passed.
