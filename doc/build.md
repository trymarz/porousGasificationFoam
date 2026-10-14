# Building porousGasificationFoam

Source OpenFOAM-v2406 (`etc/bashrc`) first. `wmake` runs in place in your checkout, which may live anywhere; `./Allwmake`, `./build.sh …` and `/abs/path/Allwmake` behave the same from any working directory.

## Common builds

```bash
./Allwmake                        # normal build: libraries, solver, utilities, no DEM
./Allwmake --yade                 # DEM build: adds the DEM library, solver gets WITH_YADE=1
./Allwclean                       # clean build state (wclean-style, keeps installed binaries)
./build.sh clean --all --purge    # blank slate; required before switching between the two builds
```

- **Normal build** — everything except DEM. This is what you want unless you couple to YADE.
- **DEM build** — see [YADE DEM coupling](#yade-dem-coupling).
- **Clean** — see [Cleaning](#cleaning). `./build.sh --help` lists every option.

## Where things land

| What | Where |
|---|---|
| Source | your checkout |
| `lnInclude/`, `Make/<WM_OPTIONS>/` | beside the source, in the checkout (git-ignored, never commit) |
| Libraries | `$FOAM_USER_LIBBIN` |
| Executables | `$FOAM_USER_APPBIN` |

Nothing is copied into `$WM_PROJECT_USER_DIR`. `$FOAM_USER_LIBBIN` is already on the runtime library path after sourcing `etc/bashrc`, so no extra environment file is needed to build or run.

## YADE DEM coupling

`--yade` enables the `DEM` library target and compiles the solver with `WITH_YADE=1`, which switches on the `#ifdef WITH_YADE` coupling code. Without it the solver has no DEM support and Yade hangs waiting for MPI communication.

It needs YADE installed with the OpenFOAM coupling module and `YADE_TRUNK` pointing at the Foam-Yade source checkout. `build.sh` checks both before compiling, so a missing prerequisite is an error rather than a linker failure, and an existing normal build is left untouched.

**Switching between normal and `--yade` needs a clean.** `wmake` recompiles by source timestamp and does not notice that `WITH_YADE` changed, so an in-place rebuild would link a mix of the two variants. `build.sh` records which variant built the current objects and refuses to build the other on top:

```bash
./build.sh clean --all --purge
./build.sh build --yade           # or ./Allwmake for the normal build
```

Use `--purge` here: it also removes the installed solver, so a rebuild that fails partway cannot leave the other variant's binary on your `PATH`. Only one variant can exist in `$FOAM_USER_APPBIN` at a time.

## Cleaning

`./Allwclean` (or `./build.sh clean …`) removes `lnInclude/` and `Make/<WM_OPTIONS>/` through `wclean`. Like `wclean`, it leaves installed libraries and executables alone: `$FOAM_USER_LIBBIN` and `$FOAM_USER_APPBIN` are shared by every project built into this `$WM_PROJECT_USER_DIR`.

`--purge` additionally deletes the libraries and executables the cleaned targets declare in their own `Make/files`, and nothing else; other libraries there, including Foam-Yade's, stay. Use it before switching build variants, or when you would rather have no solver than the previous one after a failed rebuild.

`wmake` does not prune a symlink in `lnInclude/` whose source you deleted or renamed. It dangles, so an include of it fails loudly instead of picking up stale content. Run `./Allwclean` after renaming or deleting headers.

## Fine-grained control with `build.sh`

`build.sh [build|clean]` acts on all active targets, which you switch on or off with flags; later flags override earlier ones.

```bash
./build.sh build --apps-only      # apps on, libs off
./build.sh clean --reset-all --radiationModels   # all off, then radiationModels on: clean only that
./build.sh build --all --no-DEM   # everything except DEM (this is ./Allwmake)
```

`./Allwmake` is `./build.sh build --all --no-DEM` and `./Allwclean` is `./build.sh clean --all` (which cleans DEM too); both forward extra arguments.

| Target | Type | Depends on |
|---|---|---|
| DEM (`--yade` only) | library | Foam-Yade coupling libs |
| fieldPorosityModel | library | — |
| radiationModels | library | solid thermo |
| thermophysicalModels | library suite | — |
| pyrolysisModels | library | all above |
| porousGasificationFoam | executable | all libraries |
| utilities | executable suite | — |

## Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Needs `doxygen` and `graphviz`. The wrapper finds the source root from its own location, so it can be run by absolute path from anywhere; set `POROUS_DOC_SRC` only to document a different tree.
