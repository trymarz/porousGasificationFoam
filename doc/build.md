# Building porousGasificationFoam

`pgf build` compiles PGF into a build of its own; [pgf.md](pgf.md) explains the tool around it (settings, choosing a build, running cases). This page covers what a build is, how to build with or without the DEM coupling, and how to clean up.

## Common builds

```bash
./Allwmake                        # pgf build --all --no-DEM: libraries, solver, utilities into the checkout's own build
./Allwmake --yade                 # DEM build: adds the DEM library, solver gets WITH_YADE=1
./Allwclean                       # pgf build clean --all: clean build state, keep the installed build
./Allwclean --purge               # blank slate; required before switching a checkout between the two builds
```

- **Normal build** — everything except DEM. This is what you want unless you couple to YADE.
- **DEM build** — see [YADE DEM coupling](#yade-dem-coupling).
- **Clean** — see [Cleaning](#cleaning). `utilities/pgf/build.sh --help` lists every option; `pgf build` and `./Allwmake` forward them.
- **Run** — the build is a prefix, not `$FOAM_USER_APPBIN`: run cases with `pgf run <case>` or work in `pgf shell` ([Runs](pgf.md#runs)).

## The build store

`$FOAM_USER_APPBIN` and `$FOAM_USER_LIBBIN` are shared by every worktree and project in one `$WM_PROJECT_USER_DIR`, so a plain build replaces whatever solver is there. To keep builds apart, install into a prefix:

```bash
./pgf build                                          # $PGF_BUILD_ROOT/<build key>/{bin,lib}
./pgf shell --bare -- utilities/pgf/build.sh --prefix DIR   # DIR/{bin,lib}
./pgf shell -- applications/test/regression/Allrun   # run with the checkout's build
```

- **Build key**: `<fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version>`. The fingerprint (`utilities/pgf/build.sh --fingerprint`) hashes the sources under the build targets with repo-relative paths, so two checkouts of the same sources share a key. `utilities/pgf/build.sh --print-key` and `--print-prefix` print the key and the prefix for the checkout they run in (add `--yade` for the DEM variant).
- **`PGF_BUILD_ROOT`** defaults to `$WM_PROJECT_USER_DIR/pgf-builds`. Set it on hosts without that layout.
- **Cache**: a prefix is complete once its `.pgf-build.json` exists (written last: key, fingerprint, variant, OpenFOAM version, compiler, UTC time, arguments, git `HEAD`, and under `checkout` the path, worktree, branch and whether tracked files were modified; a `--yade` build adds `foam_yade` with the Foam-Yade pin, API hash and trunk `HEAD`). `--isolated` on a complete prefix prints it and compiles nothing. Builds of the same key hold a `flock` on `$PGF_BUILD_ROOT/<key>.lock`. Each finished build also registers its checkout in `$PGF_BUILD_ROOT/.pgf/workspaces/` (path, branch, last key per variant); builds themselves are found by scanning for `.pgf-build.json`.
- **Objects** still live in `Make/<WM_OPTIONS>/` beside the sources (wmake's `-build-root` only applies inside `$WM_PROJECT_DIR`), so the variant rule below still applies per checkout.
- **DEM variant**: `--isolated --yade` (and `--prefix DIR --yade`) copies the coupling sources from `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` into `<prefix>/foam-yade/`, compiles `libMeshTree`, `libYadeComm` and `libYadeFoam` there into `<prefix>/lib` with a `.foamyade-coupling.json` stamp, and compiles PGF against those headers. The trunk is only read, so it needs no `lnInclude` and no prior build. Setting `PGF_YADE_LIBDIR` instead copies prebuilt libraries (and their stamp) from that directory. The in-tree objects are per variant: switching a checkout between `--yade` and the plain build needs `./Allwclean` (`pgf build clean --all`) first.
- Every build prints where it installs. OpenFOAM v2406 embeds no RPATH, so a prefix works anywhere as long as its `lib/` is on `LD_LIBRARY_PATH`.

`pgf build [--yade] [build.sh options]` is `pgf shell --bare [--yade] -- utilities/pgf/build.sh --isolated [--yade] [options]`.

## Building in place

`utilities/pgf/build.sh` with `etc/bashrc` sourced yourself runs `wmake` without `pgf`:

| What | Where |
|---|---|
| Source | your checkout |
| `lnInclude/`, `Make/<WM_OPTIONS>/` | beside the source, in the checkout (git-ignored, never commit) |
| Libraries | `$FOAM_USER_LIBBIN` |
| Executables | `$FOAM_USER_APPBIN` |

This is the in-place route (`utilities/pgf/build.sh` with `etc/bashrc` sourced); `pgf build` and `./Allwmake` install into a prefix instead ([The build store](#the-build-store)). Nothing is copied into `$WM_PROJECT_USER_DIR`. `$FOAM_USER_LIBBIN` is already on the runtime library path after sourcing `etc/bashrc`, so no extra environment file is needed to build or run.

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
2. **Coupling libraries** — three `wmake` libraries under `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` (`libMeshTree`, `libYadeComm`, `libYadeFoam`, in that order). `pgf build --yade` compiles them into the build (see [Isolated builds](#the-build-store)); nothing else is needed. For a non-prefix build, or to prebuild them for `PGF_YADE_LIBDIR`, build and stamp them with

   ```bash
   ./pgf shell --bare --yade -- utilities/pgf/buildCoupling.sh --libdir DIR   # default $PGF_YADE_LIBDIR
   ```

   It writes `.foamyade-coupling.json` next to the libraries: the Foam-Yade commit, the `YADE_TRUNK` used and the coupling-header hash. `build.sh --yade` compares that hash with the headers it is about to compile against and stops before `wmake` on a mismatch, printing the command above; `pgf --yade` does the same for the copy in the prefix. Both warn when `YADE_TRUNK`'s `HEAD` differs from the submodule pin, or when the libraries carry no stamp (built by hand). With `PGF_YADE_LIBDIR` set, an `--isolated --yade` build copies the libraries and the stamp into its prefix from there.
   As a last line, `porousGasificationMedia/DEM/foamYadeApiCheck.H` fails the compile by name when Foam-Yade's `FOAMYADE_COUPLING_API` differs from the one PGF needs; a Foam-Yade older than that macro is not caught there.
3. **PGF** — `./pgf build --yade` (or `utilities/pgf/build.sh build --yade` into the user dir).

`pgf shell --yade` (and `run`, `doctor`) checks the yade binary (`YADE_BIN`, or `yade` on `PATH`) against the pin. It reads the Foam-Yade commit from `yade -v` (`git-XXXXXXX`; cached per binary under `~/.cache/pgf`). The pinned commit is OK. Another commit whose coupling headers hash the same as `YADE_TRUNK`'s warns. Another coupling API fails (exit 2; `pgf setup` and `pgf doctor` show it as a WARN, since only DEM runs need yade, with the command that fixes it). A commit the trunk's git does not know, or no commit at all, warns. `PGF_YADE` stays the 0/1 switch for the DEM variant; the binary is `YADE_BIN`.

### `YADE_TRUNK`

`YADE_TRUNK` is the Foam-Yade source checkout. `porousGasificationMedia/DEM/Make/options` and the `WITH_YADE` blocks of `porousGasificationFoam/Make/options` read the coupling headers from `$(YADE_TRUNK)/pkg/openfoam/coupling/FoamYade/lnInclude` (and `meshtree/lnInclude`). Those `lnInclude/` directories appear once the coupling libraries are built, and `build.sh` checks for both.

`build.sh` and `pgf --yade` default it to `submodules/Foam-Yade`. Only `pgf` exports that default, though, and `wmake` reads `$(YADE_TRUNK)` from the environment. Outside `pgf`, export it yourself:

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
./Allwclean --purge
./Allwmake --yade                 # the DEM build; plain ./Allwmake for the normal one
```

Use `--purge` here: it also removes the installed solver, so a rebuild that fails partway cannot leave the other variant's binary behind. In place (`utilities/pgf/build.sh clean --all --purge`, then `build --yade`), only one variant can exist in `$FOAM_USER_APPBIN` at a time.

## Cleaning

`./Allwclean` (`pgf build clean --all`, or `utilities/pgf/build.sh clean …` in place) removes `lnInclude/` and `Make/<WM_OPTIONS>/` through `wclean`. Like `wclean`, it leaves installed libraries and executables alone; in place they sit in `$FOAM_USER_LIBBIN` and `$FOAM_USER_APPBIN`, shared by every project built into this `$WM_PROJECT_USER_DIR`.

`--purge` additionally deletes the libraries and executables the cleaned targets declare in their own `Make/files`, and nothing else; other libraries there, including Foam-Yade's, stay. Use it before switching build variants, or when you would rather have no solver than the previous one after a failed rebuild.

`wmake` does not prune a symlink in `lnInclude/` whose source you deleted or renamed. It dangles, so an include of it fails loudly instead of picking up stale content. Run `./Allwclean` after renaming or deleting headers.

## Fine-grained control with `build.sh`

`utilities/pgf/build.sh [build|clean]` (or `pgf build [build|clean] …`, which adds `--isolated`) acts on all active targets, which you switch on or off with flags; later flags override earlier ones.

```bash
pgf build --apps-only                          # apps on, libs off
pgf build clean --reset-all --radiationModels  # all off, then radiationModels on: clean only that
pgf build --all --no-DEM                       # everything except DEM (this is ./Allwmake)
```

`./Allwmake` is `pgf build --all --no-DEM` and `./Allwclean` is `pgf build clean --all` (which cleans DEM too); both forward extra arguments.

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
utilities/gen-compile-commands.py                   # the worktree you run it in
utilities/gen-compile-commands.py --all-worktrees   # every git worktree
```

It writes a gitignored `compile_commands.json` at each tree's root, so rerun it per machine and per worktree. It reads the `lnInclude/` directories, so build (or run `wmakeLnInclude`) first. `--with-yade` also indexes the `WITH_YADE` code paths.

## Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Needs `doxygen` and `graphviz`. The wrapper finds the source root from its own location, so it can be run by absolute path from anywhere; set `POROUS_DOC_SRC` only to document a different tree.

## Related pages

- [The pgf tool](pgf.md): settings, builds and runs.
- [Apptainer images](containers.md): `pgf pack`, an image of one commit for a server.
- [pgf reference](pgf-reference.md): every setting, choosing a build, the run record, the clean environment.
- [All documentation](README.md)
