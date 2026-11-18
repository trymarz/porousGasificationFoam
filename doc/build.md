# Building porousGasificationFoam

There is one way to build: `pgf build` (or `./Allwmake`, which runs it) compiles PGF into a prefix of its own; [pgf.md](pgf.md) explains the tool around it (settings, choosing a build, running cases). This page covers what a build is, the DEM variant, editing and rebuilding, and cleaning.

## From a fresh checkout to a run

```bash
./pgf setup                       # once: where OpenFOAM (and yade) live; puts pgf on PATH
./Allwmake                        # pgf build --all --no-DEM: libraries, solver, utilities
pgf build --yade                  # the DEM variant, beside the normal one
pgf run <case>                    # run a case with the checkout's build (or work in pgf shell)
```

- **Normal build**: everything except DEM. This is what you want unless you couple to YADE.
- **DEM build**: see [YADE DEM coupling](#yade-dem-coupling). Both variants live side by side, each in its own prefix; switch with `pgf build` / `pgf build --yade`, no clean needed ([Switching variants](#switching-variants)).
- **Run**: the build is a prefix, so run cases with `pgf run <case>` or work in `pgf shell` ([Runs](pgf.md#runs)).

## Where things land

| What | Where |
|---|---|
| Source | your checkout |
| `lnInclude/`, `Make/<WM_OPTIONS>/` | beside the source, in the checkout: wmake's object cache (git-ignored, never commit) |
| Libraries, executables | `$PGF_BUILD_ROOT/<build key>/lib`, `…/bin` (the prefix; `pgf build --print-prefix` names it) |
| wmake's output | `<prefix>/build.log`; `pgf build -v` streams it instead |

- **Build key**: `<fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version>`. The fingerprint (`pgf build --fingerprint`) hashes the sources under the build targets with repo-relative paths, so two checkouts of the same sources share a key. `pgf build --print-key` and `--print-prefix` print the key and the prefix for the checkout (add `--yade` for the DEM variant).
- **`PGF_BUILD_ROOT`** defaults to `$WM_PROJECT_USER_DIR/pgf-builds`. Set it on hosts without that layout.
- **Cache**: a prefix is complete once its `.pgf-build.json` exists (written last: key, fingerprint, variant, OpenFOAM version, compiler, UTC time, arguments, git `HEAD`, and under `checkout` the path, worktree, branch and whether tracked files were modified; a `--yade` build adds `foam_yade` with the Foam-Yade pin, API hash and trunk `HEAD`). `pgf build` on a complete prefix says so and compiles nothing, without touching the checkout. Builds of the same key hold a `flock` on `$PGF_BUILD_ROOT/<key>.lock`. Each finished build also registers its checkout in `$PGF_BUILD_ROOT/.pgf/workspaces/` (path, branch, last key per variant); builds themselves are found by scanning for `.pgf-build.json`.
- **Objects** live in `Make/<WM_OPTIONS>/` beside the sources (wmake's `-build-root` only applies inside `$WM_PROJECT_DIR`), one set per checkout; see [Switching variants](#switching-variants).
- **DEM variant**: `pgf build --yade` copies the coupling sources from `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` into `<prefix>/foam-yade/`, compiles `libMeshTree`, `libYadeComm` and `libYadeFoam` there into `<prefix>/lib` with a `.foamyade-coupling.json` stamp, and compiles PGF against those headers. The trunk is only read, so it needs no `lnInclude` and no prior build. Setting `PGF_YADE_LIBDIR` instead copies prebuilt libraries (and their stamp) from that directory.
- Every build prints where it installs. OpenFOAM v2406 embeds no RPATH, so a prefix works anywhere as long as its `lib/` is on `LD_LIBRARY_PATH`. Nothing is written to OpenFOAM's shared user directories.

`pgf build [--yade] [options]` runs the build engine `utilities/pgf/build.sh --isolated` in a clean environment (`pgf shell --bare`). The engine refuses to run without a prefix; call it through `pgf build`.

## Editing and rebuilding

After editing the sources, run `pgf build` (or `./Allwmake`) again. The new sources have a new key, so the build goes into a new prefix and recompiles only what wmake finds changed; an unchanged tree is a cache hit and returns at once. The previous prefix stays as it was for whatever still uses it; `pgf gc` removes old ones.

Do not run `wmake` yourself inside `pgf shell`: there the user output directories point at the active build's prefix, so a `wmake` would overwrite a build that its key says came from other sources. `pgf shell` refuses it with a pointer to `pgf build`. (OpenFOAM's own `wmake` for coded functions, under a case's `dynamicCode/`, is not affected.)

## Switching variants

`wmake` recompiles by source timestamp and does not notice that `WITH_YADE` changed, so the in-tree objects of the targets whose code differs between the variants (the pyrolysis library and the solver) belong to one variant at a time. `pgf build` records which variant built them; when you build the other one, it drops those targets' `Make/<WM_OPTIONS>/` first and says so:

```
INFO switching to the DEM variant: cleaned the normal objects of pyrolysisModels porousGasificationFoam; they recompile now
```

So `pgf build`, `pgf build --yade`, `pgf build` works with no clean in between. When the wanted prefix already exists, `pgf build` reuses it and leaves the objects alone.

## YADE DEM coupling

`--yade` enables the `DEM` library target and compiles the solver with `WITH_YADE=1`, which switches on the `#ifdef WITH_YADE` coupling code. Without it the solver has no DEM support and Yade hangs waiting for MPI communication.

It needs YADE installed with the OpenFOAM coupling module and `YADE_TRUNK` pointing at the Foam-Yade source checkout. `pgf build --yade` checks both before compiling, so a missing prerequisite is an error rather than a linker failure, and the normal build's prefix is left untouched.

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
2. **Coupling libraries** — three `wmake` libraries under `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` (`libMeshTree`, `libYadeComm`, `libYadeFoam`, in that order). `pgf build --yade` compiles them into the build (see [Where things land](#where-things-land)); nothing else is needed. To prebuild them for `PGF_YADE_LIBDIR`, build and stamp them with

   ```bash
   ./pgf shell --bare --yade -- utilities/pgf/buildCoupling.sh --libdir DIR   # default $PGF_YADE_LIBDIR
   ```

   It writes `.foamyade-coupling.json` next to the libraries: the Foam-Yade commit, the `YADE_TRUNK` used and the coupling-header hash. `pgf build --yade` with `PGF_YADE_LIBDIR` compares that hash with the headers it is about to compile against and stops before `wmake` on a mismatch, printing the command above; `pgf --yade` does the same for the copy in the prefix. Both warn when `YADE_TRUNK`'s `HEAD` differs from the submodule pin, or when the libraries carry no stamp (built by hand). With `PGF_YADE_LIBDIR` set, `pgf build --yade` copies the libraries and the stamp into its prefix from there.
   As a last line, `porousGasificationMedia/DEM/foamYadeApiCheck.H` fails the compile by name when Foam-Yade's `FOAMYADE_COUPLING_API` differs from the one PGF needs; a Foam-Yade older than that macro is not caught there.
3. **PGF** — `./pgf build --yade`.

`pgf shell --yade` (and `run`, `doctor`) checks the yade binary (`YADE_BIN`, or `yade` on `PATH`) against the pin. It reads the Foam-Yade commit from `yade -v` (`git-XXXXXXX`; cached per binary under `~/.cache/pgf`). The pinned commit is OK. Another commit whose coupling headers hash the same as `YADE_TRUNK`'s warns. Another coupling API fails (exit 2; `pgf setup` and `pgf doctor` show it as a WARN, since only DEM runs need yade, with the command that fixes it). A commit the trunk's git does not know, or no commit at all, warns. `PGF_YADE` stays the 0/1 switch for the DEM variant; the binary is `YADE_BIN`.

### `YADE_TRUNK`

`YADE_TRUNK` is the Foam-Yade source checkout. `porousGasificationMedia/DEM/Make/options` and the `WITH_YADE` blocks of `porousGasificationFoam/Make/options` read the coupling headers from `$(YADE_TRUNK)/pkg/openfoam/coupling/FoamYade/lnInclude` (and `meshtree/lnInclude`). `pgf build --yade` points them at its own copy in the prefix; with `PGF_YADE_LIBDIR` they are the trunk's own `lnInclude/` directories, which appear once the coupling libraries are built there, and the build checks for both.

`pgf --yade` defaults `YADE_TRUNK` to `submodules/Foam-Yade` and exports it for `wmake`; set it (or `pgf setup --yade-trunk DIR`) to use another checkout.

### Checking that the coupling is in

- **Build variant.** A `--yade` solver links the coupling libraries directly. In a normal build none of them appears:

  ```bash
  ldd $(which porousGasificationFoam) | grep -E 'libYadeFoam|libMeshTree|liblambdaDotModel'
  ```

  All three must resolve to a path; `not found` means the directory holding them is not on `LD_LIBRARY_PATH`. For a prefix build, `.pgf-build.json` in the prefix records `"variant": "yade1"`.
- **YADE side.** `yade --help` ends its description with the compiled features; the list must contain `MPI`.
- **At run time.** A `WITH_YADE` solver prints `DEM coupling: active` (or `inactive (yadeProperties absent or active=false)`) at startup, from `createDEMFields.H`; a normal build prints no such line. YADE prints `Starting OpenFOAM solver --> porousGasificationFoam` before it spawns the solver. YADE always spawns it with `-parallel`, so the case must be decomposed.

## Cleaning

`./Allwclean` (`pgf build clean --all`) cleans this checkout's in-tree cache: `lnInclude/` and `Make/<WM_OPTIONS>/` of every target, through `wclean`. Built prefixes are not touched; `pgf gc` removes those. Switching variants does not need it.

`wmake` does not prune a symlink in `lnInclude/` whose source you deleted or renamed. It dangles, so an include of it fails loudly instead of picking up stale content. Run `./Allwclean` after renaming or deleting headers.

## Choosing targets

`pgf build [clean]` acts on all active targets, which you switch on or off with flags; later flags override earlier ones. `pgf help build` and `utilities/pgf/build.sh --help` list every option.

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
