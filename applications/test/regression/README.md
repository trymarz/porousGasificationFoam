# Regression test framework

Numerical regression tests for `porousGasificationFoam`. The framework runs
selected tutorial cases, extracts a small set of summary scalars produced by
OpenFOAM `volFieldValue` function objects, and diffs the results against a
committed reference baseline within a numerical tolerance.

The point is to give refactors a safety net: any change that perturbs the
selected scalars beyond tolerance fails the regression and surfaces a clear
diff.

## What is verified against upstream OpenFOAM-com

- `volFieldValue` function-object syntax matches the verified upstream
  example at
  `tutorials/incompressible/pisoFoam/RAS/cavity/system/FOs/FOvolFieldValue`
  on `gitlab.com/openfoam/core/openfoam` (master, v2512).
- Tutorial `Allrun` scripts use the upstream `RunFunctions` helpers
  (`runApplication`, `runParallel`).

## Layout

```
applications/test/regression/
├── README.md            # this file
├── Allrun               # top-level runner: iterate cases.list, run each
├── Allclean             # clean regression artefacts in all listed cases
├── cases.list           # one tutorial path per non-comment line
└── tools/
    ├── runCase.sh       # clean -> Allrun -> compare on one case
    └── compareScalars.py  # numerical diff vs reference/ with tolerance

tutorials/cases/<caseName>/
├── system/
│   └── regressionFunctions   # included from controlDict; volFieldValue blocks
├── reference/                # committed baseline output for this case
│   └── postProcessing/...    # mirror of postProcessing/ from a known-good run
└── Allrun                    # unchanged
```

## Hooking a case into regression

Two steps per case:

1. Drop a `system/regressionFunctions` file describing the metrics to extract
   (see `tutorials/cases/charOnlyMove/system/regressionFunctions` for the
   pilot example). Add `#includeIfPresent "regressionFunctions"` at the
   bottom of `system/controlDict` inside a `functions { ... }` block.
2. Add the case path to `applications/test/regression/cases.list`.

Then capture a baseline locally (see "Capturing the baseline" below).

## Capturing the baseline

The container that hosts the AI assistant has no OpenFOAM installation; the
baseline must be captured on a machine with OpenFOAM-v2406 (or whatever
version is treated as ground truth) installed.

```sh
cd tutorials/cases/charOnlyMove
./Allclean        # if present; else remove processor*, postProcessing/, log.*
./Allrun          # produce postProcessing/
mkdir -p reference
cp -r postProcessing reference/
git add reference
git commit -m "Capture charOnlyMove regression baseline"
```

After commit, every subsequent run of `applications/test/regression/Allrun`
compares fresh output against `reference/postProcessing/` and reports
PASS/FAIL.

## Running regressions

```sh
cd applications/test/regression
./Allrun                                # run every case in cases.list
./Allrun --case charOnlyMove            # run a single case
./Allrun --rtol 1e-3                    # override default relative tolerance
```

Exit code 0 means all cases within tolerance; non-zero means at least one
case failed. The script prints a per-case PASS/FAIL summary and, on
failure, the rows that diverged.

## What the comparison does

`tools/compareScalars.py` walks `reference/postProcessing/<funcName>/<time>/`
files and, for each, locates the matching freshly-produced file under
`postProcessing/<funcName>/<time>/`. It compares row-by-row, column-by-column
with the rule:

```
pass if  abs(a - b) <= atol + rtol * max(|a|, |b|)
```

Defaults: `rtol=1e-4`, `atol=1e-12`. Both can be overridden per-run.

The header line of OpenFOAM `volFieldValue.dat` files is parsed, so column
names appear in failure reports.

## Limitations

- Comparison is over scalar reductions extracted by function objects, not
  full field comparisons. A regression that perfectly cancels in every
  selected scalar will go undetected. Add more metrics to catch a wider
  class of bugs.
- Floating-point sensitivity to MPI decomposition is real: rerun on the
  same `numberOfSubdomains` as the baseline. The pilot case `charOnlyMove`
  uses 4 procs.
- The reference baseline is only as correct as the run that produced it.
  When you change the model intentionally, regenerate the baseline and
  state why in the commit message.
