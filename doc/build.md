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

`pgf-env [--prefix DIR | --for-tree | --bare] [--yade] [--mpi loopback] -- <command>` runs a command in a clean environment. It drops the caller's environment except `HOME USER LANG TERM TMPDIR WM_PROJECT_DIR XDG_CONFIG_HOME PGF_* YADE_*` and sources `$PGF_FOAM_BASHRC` (default `$WM_PROJECT_DIR/etc/bashrc`) with `set +eu`. Then it points `WM_PROJECT_USER_DIR`, `FOAM_USER_APPBIN` and `FOAM_USER_LIBBIN` at the prefix and removes every `PATH` / `LD_LIBRARY_PATH` entry under the user dir `bashrc` chose. It also sets `UCX_VFS_ENABLE=n UCX_LOG_LEVEL=fatal`, `PGF_ISOLATED_ONLY=1` (`build.sh` then refuses to build without `--prefix` / `--isolated`) and `TMPDIR=$PGF_RUN_ROOT/tmp`. It checks that `porousGasificationFoam` resolves into the prefix and prints a one-line banner on stderr.

- `--for-tree` (default) uses this checkout's `--isolated` prefix and exits 2 if it is not built. `--bare` uses no prefix; use it to run `build.sh`.
- `--yade` adds `YADE_TRUNK` (default `submodules/Foam-Yade`), a `yade` on `PATH` (from `YADE_BIN` or the caller's `PATH`) and `PYTHONPATH` (`YADE_PYTHONPATH`, or derived from the install). If `/tmp` is read-only, it also preloads a small shim that moves Yade's `/tmp/yade-XXXXXX` into `$TMPDIR`.
- `--mpi loopback` restricts Open MPI to `self,vader,tcp` on `lo`. The DEM coupling connects Yade's MPI job to the solver it spawns over TCP, and Open MPI leaves `lo` out by default, so on a host or sandbox whose only interface is `lo` the run hangs with `help-mca-bml-r2.txt / unreachable proc`. The `dem` tier passes it; a DEM run is always on one node.

### Remembered settings

`pgf-env` reads its settings (`PGF_FOAM_BASHRC`, `YADE_TRUNK`, `YADE_BIN`, `YADE_PYTHONPATH`, `PGF_YADE_LIBDIR`, `PGF_BUILD_ROOT`, `PGF_RUN_ROOT`, `PGF_YADE=0|1`, `PGF_MPI=default|loopback|auto`) from layers; a later layer wins:

1. built-in default;
2. user file `${XDG_CONFIG_HOME:-~/.config}/pgf/env.conf`;
3. checkout file `.pgf-env.conf` at the tree root (git-ignored, so each worktree has its own);
4. the caller's environment;
5. `--set KEY=VALUE` (or `--foam-bashrc FILE`, `--yade` / `--no-yade`, `--mpi …`).

The files hold `KEY=value` lines and `#` comments. They are parsed, not sourced: an unknown key or a malformed line exits 2 naming `file:line`, and only the keys above are accepted, so a file cannot set `PATH` or run code. `PGF_YADE=1` makes `--yade` the default; `PGF_MPI=loopback` does what `--mpi loopback` does. `pgf-env status` prints every effective value and the layer (and `file:line`) it came from:

```bash
printf 'PGF_FOAM_BASHRC=%s\n' ~/OpenFOAM/OpenFOAM-v2406/etc/bashrc >> ~/.config/pgf/env.conf
./pgf-env status
```

### `init`, `shell`, `run`, `install-link`

```bash
./pgf-env init --yes                       # detect OpenFOAM and yade, write ~/.config/pgf/env.conf
./pgf-env init --checkout --yes --foam-bashrc FILE   # or this worktree's .pgf-env.conf
./pgf-env install-link                     # ~/.local/bin/pgf-env -> this checkout's pgf-env
./pgf-env shell                            # interactive bash in the env, prompt (pgf:<key>)
./pgf-env shell -c 'command -v porousGasificationFoam'
./pgf-env run tutorials/cases/<case> [Allrun | Allclean | script] [args]
```

- `init` looks for `$WM_PROJECT_DIR/etc/bashrc`, `~/OpenFOAM/*/etc/bashrc`, `/opt/openfoam*` and `/usr/lib/openfoam/*` (v2406 first), and for `yade` or a suffixed `yade-*` on your `PATH`. It asks when several candidates exist and a terminal is attached, unless `--yes`. Values given as `--foam-bashrc`, `--yade-bin`, `--yade-trunk` or `--set` win, and keys already in the file are kept. `--checkout` also records `submodules/Foam-Yade` as `YADE_TRUNK` when it is checked out.
- `shell` takes the same options as a plain call (`--yade`, `--prefix`, `--bare`, …). Nested `pgf-env` calls inside it skip the `env -i` step.
- `run` changes into the case directory (resolved from your current directory) before running, so it works from anywhere. A case whose `constant/yadeProperties` says `active true` gets `--yade` unless `--no-yade` is given or `PGF_YADE` is set in a file or the environment.
- `install-link` refuses to replace a link to another checkout without `--force`. With several worktrees, the link picks one; call the others by path.

Tests for this tooling live in `applications/test/tooling/test_*.sh`; each runs alone with `PGF_FOAM_BASHRC` set and builds under `PGF_BUILD_ROOT` (default `$TMPDIR/pgf-tooling-builds`).

## YADE DEM coupling

`--yade` enables the `DEM` library target and compiles the solver with `WITH_YADE=1`, which switches on the `#ifdef WITH_YADE` coupling code. Without it the solver has no DEM support and Yade hangs waiting for MPI communication.

It needs YADE installed with the OpenFOAM coupling module and `YADE_TRUNK` pointing at the Foam-Yade source checkout. `build.sh` checks both before compiling, so a missing prerequisite is an error rather than a linker failure, and an existing normal build is left untouched.

### Supported versions

- **OpenFOAM v2406**, the solver's only target. The coupling libraries are compiled against the same OpenFOAM.
- **YADE from Foam-Yade** ([trymarz/Foam-Yade](https://github.com/trymarz/Foam-Yade)), a YADE fork, at the commit this repository pins. Upstream YADE does not work: the per-particle `lambda_`/`lambdaDot` state, the λ slot in the force buffer and the `lambdaMode` startup handshake exist only in the fork. Its version string is the build date and commit, e.g. `yade -v` prints `Yade version: 2026-10-15.git-d5c33e0`.
- **One MPI for both.** YADE starts the solver with `MPI_Comm_spawn` and merges the two jobs into one communicator, so YADE, the coupling libraries and OpenFOAM must use the same MPI installation (Open MPI here).
- **Both sides from the same revision.** The per-particle buffer layout is fixed at compile time on each side with no negotiation over MPI (`FoamCoupling::sendBodyData()` and `FoamYade::locateAllParticles()` both say so). A YADE built from another Foam-Yade commit than the coupling libraries misreads the buffers instead of failing.

### The submodule and its pin

Foam-Yade is the submodule `submodules/Foam-Yade` (`.gitmodules`). The pinned commit is the gitlink in this repository's tree:

```bash
git submodule update --init submodules/Foam-Yade
git ls-tree HEAD submodules/Foam-Yade     # the pinned commit
git submodule status submodules/Foam-Yade # a leading '+' means the checkout differs from the pin
```

When the pin moves, rebuild YADE, the coupling libraries and PGF's `--yade` build together. The `yade1` build key folds in the pin (as staged in the index) and a hash of the coupling headers under `YADE_TRUNK` (`FoamYade.H`, `meshtree/meshTree.H`, `commYade/yadeComm.H`), so a moved pin or other headers give a new prefix instead of a stale cache hit. The `yade0` key ignores both.

### Building YADE and the coupling libraries

1. **YADE** — a CMake build of the Foam-Yade checkout. The installation guide is upstream's (`doc/sphinx/installation.rst` in the submodule). The coupling needs `ENABLE_MPI` (on by default), Python 3 and `mpi4py`; without them CMake prints `MPI NOT FOUND, YADE-OpenFOAM coupling disabled`. The YADE side of the coupling, the `FoamCoupling` engine, is `pkg/openfoam/FoamCoupling.cpp`, built into `libpkg_openfoam.so`.
2. **Coupling libraries** — three `wmake` libraries under `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` (`libMeshTree`, `libYadeComm`, `libYadeFoam`, in that order). Build and stamp them with

   ```bash
   ./pgf-env --bare --yade -- utilities/pgf-env/buildCoupling.sh --libdir DIR   # default $PGF_YADE_LIBDIR
   ```

   It writes `.foamyade-coupling.json` next to the libraries: the Foam-Yade commit, the `YADE_TRUNK` used and the coupling-header hash. `build.sh --yade` compares that hash with the headers it is about to compile against and stops before `wmake` on a mismatch, printing the command above; `pgf-env --yade` does the same for the copy in the prefix. Both warn when `YADE_TRUNK`'s `HEAD` differs from the submodule pin, or when the libraries carry no stamp (built by hand). An `--isolated --yade` build copies the libraries and the stamp into its prefix from `PGF_YADE_LIBDIR` (default: the user lib dir OpenFOAM's `bashrc` sets).
   As a last line, `porousGasificationMedia/DEM/foamYadeApiCheck.H` fails the compile by name when Foam-Yade's `FOAMYADE_COUPLING_API` differs from the one PGF needs; a Foam-Yade older than that macro is not caught there.
3. **PGF** — `./build.sh build --yade`, or `./pgf-env --bare --yade -- ./build.sh --isolated --yade` for a prefix build.

### `YADE_TRUNK`

`YADE_TRUNK` is the Foam-Yade source checkout. `porousGasificationMedia/DEM/Make/options` and the `WITH_YADE` blocks of `porousGasificationFoam/Make/options` read the coupling headers from `$(YADE_TRUNK)/pkg/openfoam/coupling/FoamYade/lnInclude` (and `meshtree/lnInclude`). Those `lnInclude/` directories appear once the coupling libraries are built, and `build.sh` checks for both.

`build.sh` and `pgf-env --yade` default it to `submodules/Foam-Yade`. Only `pgf-env` exports that default, though, and `wmake` reads `$(YADE_TRUNK)` from the environment. Outside `pgf-env`, export it yourself:

```bash
export YADE_TRUNK=$PWD/submodules/Foam-Yade
```

### Checking that the coupling is in

- **Build variant.** A `--yade` solver links the coupling libraries directly. In a normal build none of them appears:

  ```bash
  ldd $(which porousGasificationFoam) | grep -E 'libYadeFoam|libMeshTree|liblambdaDotModel'
  ```

  All three must resolve to a path; `not found` means the directory holding them is not on `LD_LIBRARY_PATH`. For a prefix build, `.pgf-build.json` in the prefix records `"variant": "yade1"`.
- **YADE side.** `yade --help` ends its description with the compiled features; the list must contain `MPI`.
- **At run time.** A `WITH_YADE` solver prints `DEM coupling: active` (or `inactive (yadeProperties absent or active=false)`) at startup, from `createDEMFields.H`; a normal build prints no such line. YADE prints `Starting OpenFOAM solver --> porousGasificationFoam` before it spawns the solver. YADE always spawns it with `-parallel`, so the case must be decomposed.

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

## Editor navigation (clangd)

For go-to-definition, find-references and diagnostics in any clangd-based editor (nvim, VS Code, ...), generate a compilation database from the `Make/files` and `Make/options` of each component:

```bash
source <OpenFOAM>/etc/bashrc        # sets WM_PROJECT_DIR; or pass --foam DIR
utilities/gen-compile-commands.py                   # this worktree
utilities/gen-compile-commands.py --all-worktrees   # every git worktree
```

It writes a gitignored `compile_commands.json` at each tree's root, so rerun it per machine and per worktree. It reads the `lnInclude/` directories, so build (or run `wmakeLnInclude`) first. `--with-yade` also indexes the `WITH_YADE` code paths.

## Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Needs `doxygen` and `graphviz`. The wrapper finds the source root from its own location, so it can be run by absolute path from anywhere; set `POROUS_DOC_SRC` only to document a different tree.
