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

## Isolated builds and `pgf-env`

`$FOAM_USER_APPBIN` and `$FOAM_USER_LIBBIN` are shared by every worktree and project in one `$WM_PROJECT_USER_DIR`, so a plain build replaces whatever solver is there. To keep builds apart, install into a prefix:

```bash
./pgf-env --bare -- ./build.sh --isolated          # $PGF_BUILD_ROOT/<build key>/{bin,lib}
./pgf-env --bare -- ./build.sh --prefix DIR        # DIR/{bin,lib}
./pgf-env -- applications/test/regression/Allrun   # run with this tree's prefix
```

- **Build key**: `<fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version>`. The fingerprint (`./build.sh --fingerprint`) hashes the sources under the build targets with repo-relative paths, so two checkouts of the same sources share a key. `./build.sh --print-key` and `--print-prefix` print the key and the prefix for this tree (add `--yade` for the DEM variant).
- **`PGF_BUILD_ROOT`** defaults to `$WM_PROJECT_USER_DIR/pgf-builds`. Set it on hosts without that layout.
- **Cache**: a prefix is complete once its `.pgf-build.json` exists (written last: key, fingerprint, variant, OpenFOAM version, compiler, UTC time, arguments, git `HEAD`). `--isolated` on a complete prefix prints it and compiles nothing. Builds of the same key hold a `flock` on `$PGF_BUILD_ROOT/<key>.lock`.
- **Objects** still live in `Make/<WM_OPTIONS>/` beside the sources (wmake's `-build-root` only applies inside `$WM_PROJECT_DIR`), so the variant rule below still applies per checkout.
- **DEM variant**: `--isolated --yade` copies `libMeshTree.so`, `libYadeFoam.so` and `libYadeComm.so` into the prefix from `PGF_YADE_LIBDIR` (default: the user lib dir OpenFOAM's `bashrc` sets), and fails if any is missing.
- Every build prints where it installs. OpenFOAM v2406 embeds no RPATH, so a prefix works anywhere as long as its `lib/` is on `LD_LIBRARY_PATH`.

`pgf-env [--prefix DIR | --for-tree | --bare] [--yade] [--mpi loopback] -- <command>` runs a command in a clean environment. It drops the caller's environment except `HOME USER LANG TERM TMPDIR WM_PROJECT_DIR PGF_* YADE_*` and sources `$PGF_FOAM_BASHRC` (default `$WM_PROJECT_DIR/etc/bashrc`) with `set +eu`. Then it points `WM_PROJECT_USER_DIR`, `FOAM_USER_APPBIN` and `FOAM_USER_LIBBIN` at the prefix and removes every `PATH` / `LD_LIBRARY_PATH` entry under the user dir `bashrc` chose. It also sets `UCX_VFS_ENABLE=n UCX_LOG_LEVEL=fatal`, `PGF_ISOLATED_ONLY=1` (`build.sh` then refuses to build without `--prefix` / `--isolated`) and `TMPDIR=$PGF_RUN_ROOT/tmp`. It checks that `porousGasificationFoam` resolves into the prefix and prints a one-line banner on stderr.

- `--for-tree` (default) uses this checkout's `--isolated` prefix and exits 2 if it is not built. `--bare` uses no prefix; use it to run `build.sh`.
- `--yade` adds `YADE_TRUNK` (default `submodules/Foam-Yade`), a `yade` on `PATH` (from `YADE_BIN` or the caller's `PATH`) and `PYTHONPATH` (`YADE_PYTHONPATH`, or derived from the install). If `/tmp` is read-only, it also preloads a small shim that moves Yade's `/tmp/yade-XXXXXX` into `$TMPDIR`.
- `--mpi loopback` restricts Open MPI to `self,vader,tcp` on `lo`.

Tests for this tooling live in `applications/test/tooling/test_*.sh`; each runs alone with `PGF_FOAM_BASHRC` set and builds under `PGF_BUILD_ROOT` (default `$TMPDIR/pgf-tooling-builds`).

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
