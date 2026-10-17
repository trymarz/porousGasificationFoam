# Regression testing

Refactor safety net: the framework runs selected tutorial cases, extracts summary scalars (OpenFOAM `volFieldValue` function objects) and diffs them against a committed baseline within a tolerance. It lives in `applications/test/regression/` and needs only bash, `python3` and the OpenFOAM environment.

## Run it

Source OpenFOAM and build the solver first (a missing solver gives `ERROR`/`CRASH`, never a false green).

```bash
cd applications/test/regression
./Allrun                                  # fast tier, one case at a time
./Allrun --tag all                        # every case (adds the convergence tier)
./Allrun --case charOnlyMoveCases/serial  # one case
./Allrun --list                           # show the selection, run nothing
./Allrun --no-run                         # compare existing output only
./Allrun --rtol 1e-3                      # override relative tolerance
./Allrun --jobs 4                         # four cases at once
```

Exit `0` = every case passed; non-zero = at least one did not. A single case without the suite: `tools/runCase.sh tutorials/cases/canonical/pyrolysis [--no-run] [--rtol R] [--atol A]`.

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

## Add a case

1. Add `system/regressionFunctions` with the metrics to extract (example: `tutorials/cases/charOnlyMoveCases/serial/system/regressionFunctions`), and `#includeIfPresent "regressionFunctions"` inside `functions { … }` at the bottom of `system/controlDict`.
2. Add the case path (and optional tag) to `applications/test/regression/cases.list`.
3. Capture and commit a baseline (a case without one fails the suite, so register and baseline together):

```bash
cd tutorials/cases/charOnlyMoveCases/serial
./Allclean && ./Allrun
mkdir -p reference && cp -r postProcessing reference/
git add reference && git commit -m "Capture charOnlyMoveCases/serial regression baseline"
```

The baseline is only as correct as the run that made it. After an intentional model change, regenerate it and say why in the commit message.

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

- Serial suite: `Allrun`/`Allclean`, `cases.list`, `tools/runCase.sh`, `tools/compareScalars.py`. DEM suite: `Allrun.yade`, `cases_yade.list`, `tools/runDEMCase.sh`.
- `tools/regressionLib.sh` holds what both runners must agree on: loading the case list, mapping exit status to outcome, deciding if the suite is green, and bounded concurrency. `tools/regressionState.py` writes run state as JSON (standard library only).
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
