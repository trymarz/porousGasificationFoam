# Building porousGasificationFoam

There is one way to build: `pgf build` (or `./Allwmake`, which runs it) compiles PGF into a prefix of its own; [pgf.md](pgf.md) explains the tool around it (settings, choosing a build, running cases). This page covers what a build is, the DEM variant, editing and rebuilding, and cleaning.

## From a fresh checkout to a run

```bash
./pgf setup                       # once: where OpenFOAM (and yade) live; puts pgf on PATH
./Allwmake                        # pgf build --all --no-DEM: libraries and solver
pgf build --yade                  # the DEM variant, beside the normal one
pgf run <case>                    # run a case with the checkout's build (or work in pgf shell)
```

- **Normal build**: everything except DEM. This is what you want unless you couple to YADE.
- **DEM build**: see [YADE DEM coupling](#yade-dem-coupling). Both variants live side by side, each in its own prefix; switch with `pgf build` / `pgf build --yade`, no clean needed ([Switching variants](#switching-variants)).
- **Run**: the build is a prefix, so run cases with `pgf run <case>` or work in `pgf shell` ([Runs](pgf.md#runs)).

## How a build works

pgf decides *what* to build and *where* it goes; OpenFOAM's own `wmake` does the compiling:

1. **pgf build** (or `./Allwmake`) names the build from your sources, the variant (normal or DEM), the OpenFOAM version and the compiler, in a clean shell that loads OpenFOAM.
2. **Built before?** Then it is reused at once: nothing compiles, the checkout is not touched.
3. **Variant switched** between normal and DEM since the last build? pgf cleans the 2 parts that differ (pyrolysis library, solver) so they recompile; you never clean by hand.
4. **wmake** (OpenFOAM's own tool) compiles each part. It keeps compiled pieces next to the source (the **worktree cache**, which makes the next build quick) and puts the solver and libraries in the **build folder**, `pgf-builds/<key>/`.
5. **pgf run** (and `pgf shell`) run cases with that build folder.

`./Allwclean` empties the worktree cache; `pgf gc` removes old build folders.

`utilities/pgf/build.sh` is the engine behind step 4, not a second way in: called directly, it exits with `needs --isolated or --prefix DIR` and names the `pgf build` command to run instead.

## Where things land

| What | Where |
|---|---|
| Source | your checkout |
| `lnInclude/`, `Make/<WM_OPTIONS>/` | beside the source, in the checkout: wmake's object cache (git-ignored, never commit) |
| Libraries, executables | `$PGF_BUILD_ROOT/<build key>/lib`, `…/bin` (the prefix; `pgf build --print-prefix` names it) |
| wmake's output | `<prefix>/build.log`; `pgf build -v` streams it instead |

- **Build key**: `<fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version>`. The fingerprint (`pgf build --fingerprint`) hashes the sources under the build targets with repo-relative paths, so two checkouts of the same sources share a key. `pgf build --print-key` and `--print-prefix` print the key and the prefix for the checkout (add `--yade` for the DEM variant).
- **`PGF_BUILD_ROOT`** defaults to `$WM_PROJECT_USER_DIR/pgf-builds`. Set it on hosts without that layout.
- **Cache**: a prefix is complete once its `.pgf-build.json` exists (written last: key, fingerprint, variant, OpenFOAM version, compiler, UTC time, arguments, git `HEAD`, and under `checkout` the path, worktree, branch and whether tracked files were modified; a `--yade` build adds `foam_yade` with the yade-pgf pin, the `PgfCoupling` hash and the trunk `HEAD`, for the record only). `pgf build` on a complete prefix says so and compiles nothing, without touching the checkout. Builds of the same key hold a `flock` on `$PGF_BUILD_ROOT/<key>.lock`. Each finished build also registers its checkout in `$PGF_BUILD_ROOT/.pgf/workspaces/` (path, branch, last key per variant); builds themselves are found by scanning for `.pgf-build.json`.
- **Objects** live in `Make/<WM_OPTIONS>/` beside the sources (wmake's `-build-root` only applies inside `$WM_PROJECT_DIR`), one set per checkout; see [Switching variants](#switching-variants).
- **DEM variant**: `pgf build --yade` compiles nothing from Yade. The OpenFOAM half of the coupling, `pgfYadeCoupler`, is PGF code that needs only OpenFOAM and MPI.
- Every build prints where it installs. OpenFOAM v2606 embeds no RPATH, so a prefix works anywhere as long as its `lib/` is on `LD_LIBRARY_PATH`. Nothing is written to OpenFOAM's shared user directories.

`pgf build [--yade] [options]` runs the build engine `utilities/pgf/build.sh --isolated` in a clean environment (`pgf shell --bare`). The engine refuses to run without a prefix; call it through `pgf build`.

## Editing and rebuilding

After editing the sources, run `pgf build` (or `./Allwmake`) again. The new sources have a new key, so the build goes into a new prefix and recompiles only what wmake finds changed; an unchanged tree is a cache hit and returns at once. The previous prefix stays as it was for whatever still uses it; `pgf gc` removes old ones.

Do not run `wmake` yourself inside `pgf shell`: there the user output directories point at the active build's prefix, so a `wmake` would overwrite a build that its key says came from other sources. `pgf shell` refuses it with `pgf: refusing wmake: it would install into the cached build <key>.` and a pointer to `pgf build`. (OpenFOAM's own `wmake` for coded functions, under a case's `dynamicCode/`, is not affected.)

## Switching variants

`wmake` recompiles by source timestamp and does not notice that `WITH_YADE` changed, so the in-tree objects of the targets whose code differs between the variants (the pyrolysis library and the solver) belong to one variant at a time. `pgf build` records which variant built them; when you build the other one, it drops those targets' `Make/<WM_OPTIONS>/` first and says so:

```
INFO switching to the DEM variant: cleaned the normal objects of pyrolysisModels porousGasificationFoam; they recompile now
```

So `pgf build`, `pgf build --yade`, `pgf build` works with no clean in between. When the wanted prefix already exists, `pgf build` reuses it and leaves the objects alone.

## YADE DEM coupling

`--yade` enables the `DEM` library target and compiles the solver with `WITH_YADE=1`, which switches on the `#ifdef WITH_YADE` coupling code. Without it the solver has no DEM support and Yade hangs waiting for MPI communication.

Building needs no YADE. Running a DEM case needs a YADE built with MPI from yade-pgf at the pin, which carries the `PgfCoupling` engine. [install.md](install.md#3-dem-coupling-optional) walks through it on a clean machine; this section explains it.

### Supported versions

- **OpenFOAM v2606**, the solver's only target.
- **YADE from yade-pgf** ([trymarz/yade-pgf](https://github.com/trymarz/yade-pgf), branch `pgf-main`), a YADE fork, at the commit this repository pins. Upstream YADE does not work: the per-particle `lambda_`/`lambdaDot` state, and the `PgfCoupling` engine exist only in the fork. Built as in [install.md](install.md#3-dem-coupling-optional), its version string carries the commit, e.g. `yade -v` prints `Yade version: 2026.11.25-git-475bb35`; without `-DYADE_VERSION` it is the bare `2026.1.0` of the fork's `RELEASE` file.
- **One MPI for both.** YADE starts the solver with `MPI_Comm_spawn` and merges the two jobs into one communicator, so YADE and OpenFOAM must use the same MPI installation (Open MPI here).
- **Same protocol on both sides.** PGF links nothing from YADE and includes none of its headers. `pgfYadeCoupler` and `PgfCoupling` each hard-code wire protocol v1; the startup handshake compares version, strides and `lambdaMode`, and stops both sides by name on a mismatch ([theory-DEM.md](theory-DEM.md#exchanged-fields)). This run-time check replaces any build-time check of the buffer layout.

### The submodule and its pin

yade-pgf is the submodule `submodules/yade-pgf` (`.gitmodules`). The pinned commit is the gitlink in this repository's tree:

```bash
git submodule update --init submodules/yade-pgf
git ls-tree HEAD submodules/yade-pgf     # the pinned commit
git submodule status submodules/yade-pgf # a leading '+' means the checkout differs from the pin
```

When the pin moves, rebuild YADE. PGF's `--yade` build does not depend on the pin, so its key ignores it.

`submodules/PgfDemBridge` holds the core of coupling v2 ([#328](https://github.com/trymarz/porousGasificationFoam/issues/328), [PgfDemBridge](https://github.com/trymarz/PgfDemBridge), GPL-3.0-or-later). `pgf build --yade` compiles it into `liblambdaDotModel` (`porousGasificationMedia/DEM/pgfDemCoupler/PgfDemBridge.C`), so the submodule must be checked out (`git submodule update --init submodules/PgfDemBridge`). yade-pgf carries the same repository as `extra/PgfDemBridge`, and both pins must match.

### Building YADE

1. **YADE** — a CMake build of the yade-pgf checkout at the pin; [install.md](install.md#3-dem-coupling-optional) has the package list and the CMake line, both taken from `pgf pack` (`utilities/pgf/pack.sh`). The coupling needs `ENABLE_MPI` (on by default), Python 3 and `mpi4py`; without them CMake prints `MPI NOT FOUND, YADE-OpenFOAM coupling disabled`. The YADE side of the coupling, the `PgfCoupling` engine, is `pkg/openfoam/PgfCoupling.cpp`, built into `libpkg_openfoam.so`.
2. **PGF** — `./pgf build --yade`.

`pgf shell --yade` (and `run`, `status`, `doctor`, `test dem`) checks the yade binary (`YADE_BIN`, or `yade` on `PATH`) against the pin. It reads the yade-pgf commit from `yade -v` (`git-XXXXXXX`; cached per binary under `~/.cache/pgf`) and, when `YADE_TRUNK` or the submodule is a git checkout that has the pin, compares the `mpy.py` installed beside yade with the pin's `py/mpy.py`. The version string is set when yade is configured (`-DYADE_VERSION` overrides it), so the file decides when the two disagree. The pin, or the pin's `mpy.py`, is OK. Another commit warns, naming how far it is from the pin (for example `6 commits older than the pin`): a fix inside `mpy.py` leaves `PgfCoupling.{hpp,cpp}` alone, so a same-API build can still give other results. Another coupling API (other `PgfCoupling` sources) fails (exit 2; `pgf setup`, `pgf status` and `pgf doctor` show it as a WARN, since only DEM runs need yade, with the command that fixes it). No commit at all warns. `pgf test dem` refuses, before building, any yade that is not OK, since its baselines were made with the pin; `PGF_YADE_OFF_PIN=1 pgf test dem` runs it anyway and says so. `PGF_YADE` stays the 0/1 switch for the DEM variant; the binary is `YADE_BIN`.

### `YADE_TRUNK`

`YADE_TRUNK` is the yade-pgf source checkout. The build does not read it; `pgf` uses it for the pin checks above (its `PgfCoupling` sources and `py/mpy.py`).

`pgf --yade` defaults `YADE_TRUNK` to `submodules/yade-pgf`; set it (or `pgf setup --yade-trunk DIR`) to use another checkout.

### Checking that the coupling is in

- **Build variant.** A `--yade` solver links `liblambdaDotModel` (which holds `pgfYadeCoupler`). In a normal build it does not appear:

  ```bash
  ldd $(which porousGasificationFoam) | grep liblambdaDotModel
  ```

  It must resolve to a path; `not found` means the directory holding them is not on `LD_LIBRARY_PATH`. For a prefix build, `.pgf-build.json` in the prefix records `"variant": "yade1"`.
- **YADE side.** `yade --help` ends its description with the compiled features; the list must contain `MPI`.
- **At run time.** A `WITH_YADE` solver prints `DEM coupling: active` (or `inactive (yadeProperties absent or active=false)`) at startup, from `createDEMFields.H`; a normal build prints no such line. YADE prints `Starting OpenFOAM solver --> porousGasificationFoam` before it spawns the solver. YADE always spawns it with `-parallel`, so the case must be decomposed.
- **Handshake.** Once both sides agree on protocol v1, the solver prints `pgfYadeCoupler: handshake ok, protocol v1, lambdaMode …, N Yade worker(s)` and YADE prints `PgfCoupling: handshake ok, protocol v1, lambdaMode …, N OpenFOAM ranks`; a tutorial's `Allrun` keeps both in `log.yade`. A mismatch stops the run with a message naming both versions; a DEM script without `PgfCoupling` makes the solver stop after `handshakeTimeout` (`constant/yadeProperties`, default 60 s).

### ParaView and Yade's VTK

YADE is configured with `ENABLE_VTK=ON` for `VTKRecorder` (C++, in `libpost_vtk.so`), which DEM cases such as `MicroTGA-DEM_lambdaDot_common` use for their sphere output. On Debian 13 that means two packages with different jobs:

| Package | Needed for |
|---|---|
| `libvtk9-dev` (depends on `python3-vtk9`) | configuring and compiling YADE (`VTK_DIR=/usr/lib/x86_64-linux-gnu/cmake/vtk-9.3`, the headers) |
| `libvtk9.3` | running YADE: the `libvtk*-9.3.so.1` that `libyade.so`, `libpost_vtk.so` and others link |

Neither YADE's Python modules nor PGF's DEM scripts import the `vtk` Python module: `yade.export.VTKExporter` and the cases' `MPI_lambda.py` write VTK files as text. `pgf build --yade` does not need VTK either.

`python3-paraview`, which [`pgf view`](paraview.md) needs, conflicts with `python3-vtk9` (both ship the `vtkmodules` Python package), so `libvtk9-dev` and `python3-paraview` cannot be installed together. The route that works:

1. Day to day, keep `paraview python3-paraview` and the runtime `libvtk9.3`. `libvtk9.3` is usually marked as automatically installed, so keep `apt autoremove` from taking it: `sudo apt-mark manual libvtk9.3`.
2. To build or rebuild YADE (for example when the pin moves), swap: `sudo apt install libvtk9-dev` (removes `python3-paraview`), build YADE, then `sudo apt install python3-paraview` (removes `libvtk9-dev` and `python3-vtk9` again). The installed YADE keeps working; `ldd <yade lib dir>/libpost_vtk.so | grep 'not found'` should print nothing.
3. If you need both at once, use a Kitware ParaView binary (its own Python and VTK, no apt packages) with its `bin/` first on `PATH`, and keep `libvtk9-dev`.

`pgf doctor` reports, under "ParaView", whether the ParaView on your `PATH` has Python.

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
| DEM (`--yade` only) | library | — |
| fieldPorosityModel | library | — |
| radiationModels | library | solid thermo |
| thermophysicalModels | library suite | — |
| pyrolysisModels | library | all above |
| porousGasificationFoam | executable | all libraries |

## Editor and agent navigation (clangd, Serena)

For go-to-definition, find-references and diagnostics in any clangd-based editor (nvim, VS Code, ...), generate a compilation database from the `Make/files` and `Make/options` of each component:

```bash
source <OpenFOAM>/etc/bashrc        # sets WM_PROJECT_DIR; or pass --foam DIR
utilities/gen-compile-commands.py                   # the worktree you run it in
utilities/gen-compile-commands.py --all-worktrees   # every git worktree
```

It writes a gitignored `compile_commands.json` at each tree's root, so rerun it per machine and per worktree. It reads the `lnInclude/` directories, so build (or run `wmakeLnInclude`) first. The C++ standard comes from OpenFOAM's `wmake/rules`, as in a real build. `--with-yade` also indexes the `WITH_YADE` code paths; without it that code is greyed out.

Rerun it after adding, removing or renaming a `.C` file or a header, or after editing a `Make/files` or `Make/options`. clangd picks up edits to existing files by itself, but not a changed list of files.

`utilities/gen-compile-commands.py --if-stale` does that only when needed: it regenerates the database (in well under a second) when the list of sources or a `Make/files`/`Make/options` changed since its last run, warns when an `lnInclude/` lacks a link for a new or renamed file, and is silent otherwise. Claude Code runs it at session start and after each edit of a `.C`, `.H` or `Make/` file (`.claude/settings.json`). Git runs it after checkout, merge and rebase once the repository's hooks are enabled:

```bash
git config core.hooksPath utilities/githooks   # once per clone; covers all its worktrees
```

`pgf build` relinks `lnInclude/`. A cached build compiles nothing and leaves it alone; then `./pgf shell --bare -- wmakeLnInclude -update <component dir>` adds the missing links.

Claude Code sessions in this repository also start [Serena](https://github.com/oraios/serena) (`.mcp.json`, `.serena/project.yml`), which gives the agent symbol lookups over the same database. One command sets it up; rerun it in any worktree that was never built or indexed:

```bash
utilities/serena-setup                                  # this worktree
utilities/serena-setup --all                            # every worktree with Serena config
utilities/serena-setup --with foamcli --with yade-pgf   # also these sibling repositories
utilities/serena-setup --with-submodules                # also submodules/yade-pgf, if checked out
```

It installs Serena with `uv tool install` into `~/.local` when missing, enables the git hooks, links `lnInclude/` without compiling, writes `compile_commands.json` and indexes the worktree (minutes on a first run). A new worktree's index starts from the newest one among the other worktrees when their files share the same compile flags, so only the files that differ are indexed (seconds instead of minutes). `--fresh` rebuilds an index from scratch.

Sibling repositories are set up only when named with `--with`, or given by path. They get a local Serena config (`.serena/`, `.mcp.json`, kept out of git through `.git/info/exclude`) and an index. A Python repository such as foamcli needs nothing else. A CMake repository such as yade-pgf gets a configure-only CMake run (no compile, about 10 s) in `~/.cache/serena-setup/`; its compile database goes to the repository's root without the GCC-only flags clangd rejects. YADE is configured as in the container recipe (`-DENABLE_GUI=OFF`); `SERENA_CMAKE_ARGS` adds options. These repositories have no freshness hooks, so rerun the command after adding or removing sources there.

`--with-submodules` treats each PGF worktree's checked-out `submodules/yade-pgf` the same way, as a project of its own (its excludes go to the submodule's git directory); a submodule that is not checked out is skipped with a note. The worktree's own index still ignores `submodules/`, so a session started in the worktree does not see YADE symbols; start one in the submodule for that.

Serena runs locally and calls no AI service; it hands clangd's answers to the agent. Its first start downloads clangd into `~/.serena`. `.mcp.json` turns off its anonymous usage ping and keeps its web dashboard from opening a browser; the dashboard (logs and tool calls) stays reachable on 127.0.0.1 and checks for Serena updates.

`.mcp.json` finds `serena` on `PATH` or in `~/.local/bin`. Claude Code asks once per project to approve the server. A session opened at the bare repository's root, outside every worktree, has no `.mcp.json` and so no Serena. Every worktree's project is named `porousGasificationFoam`; Serena finds it by path, so worktrees can be open side by side.

## Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Needs `doxygen` and `graphviz`. The wrapper finds the source root from its own location, so it can be run by absolute path from anywhere; set `POROUS_DOC_SRC` only to document a different tree.

## Related pages

- [The pgf tool](pgf.md): settings, builds and runs.
- [Apptainer images](containers.md): `pgf pack`, an image of one commit for a server.
- [All documentation](README.md)
