# Building porousGasificationFoam

Source OpenFOAM-v2406 (`etc/bashrc`) first. `wmake` runs in place in your checkout, which may live anywhere; `./Allwmake`, `./build.sh …` and `/abs/path/Allwmake` behave the same from any working directory.

## First-time setup

In a new terminal, with nothing sourced or exported:

```bash
./pgf setup --yes                # finds OpenFOAM's etc/bashrc and yade; writes ~/.config/pgf/env.conf; links ~/.local/bin/pgf
pgf status                       # what was recorded, and from which file
pgf doctor                       # checks, each with a fix
pgf build                        # build this tree into its own prefix
pgf run tutorials/cases/canonical/pyrolysis    # run a case from anywhere
pgf shell                        # or work interactively in the same environment
```

For the DEM variant use `pgf build --yade`; it compiles the [coupling libraries](#building-yade-and-the-coupling-libraries) into the build itself; `pgf run` picks `--yade` for a case with an active `constant/yadeProperties`. Settings that differ per worktree go in `pgf config set KEY=VALUE --checkout`. See [Remembered settings](#remembered-settings).

`pgf` replaced `pgf-env` (#264). `pgf-env` still works as a deprecated alias: `pgf-env [options] -- CMD` is `pgf exec [options] -- CMD`, `pgf-env init` is `pgf setup --no-link`, `pgf-env install-link` is `pgf setup --link-only`, and `status`, `doctor`, `shell` and `run` keep their names. It prints a deprecation note on stderr unless `PGF_ENV_QUIET=1`.

The rest of this page covers the plain `wmake` route as well, where you source `etc/bashrc` yourself.

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

## Isolated builds and `pgf`

`$FOAM_USER_APPBIN` and `$FOAM_USER_LIBBIN` are shared by every worktree and project in one `$WM_PROJECT_USER_DIR`, so a plain build replaces whatever solver is there. To keep builds apart, install into a prefix:

```bash
./pgf build                                          # $PGF_BUILD_ROOT/<build key>/{bin,lib}
./pgf exec --bare -- ./build.sh --prefix DIR         # DIR/{bin,lib}
./pgf exec -- applications/test/regression/Allrun    # run with this tree's prefix
```

- **Build key**: `<fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version>`. The fingerprint (`./build.sh --fingerprint`) hashes the sources under the build targets with repo-relative paths, so two checkouts of the same sources share a key. `./build.sh --print-key` and `--print-prefix` print the key and the prefix for this tree (add `--yade` for the DEM variant).
- **`PGF_BUILD_ROOT`** defaults to `$WM_PROJECT_USER_DIR/pgf-builds`. Set it on hosts without that layout.
- **Cache**: a prefix is complete once its `.pgf-build.json` exists (written last: key, fingerprint, variant, OpenFOAM version, compiler, UTC time, arguments, git `HEAD`, and under `checkout` the path, worktree, branch and whether tracked files were modified; a `--yade` build adds `foam_yade` with the Foam-Yade pin, API hash and trunk `HEAD`). `--isolated` on a complete prefix prints it and compiles nothing. Builds of the same key hold a `flock` on `$PGF_BUILD_ROOT/<key>.lock`. Each finished build also registers its checkout in `$PGF_BUILD_ROOT/.pgf/workspaces/` (path, branch, last key per variant); builds themselves are found by scanning for `.pgf-build.json`.
- **Objects** still live in `Make/<WM_OPTIONS>/` beside the sources (wmake's `-build-root` only applies inside `$WM_PROJECT_DIR`), so the variant rule below still applies per checkout.
- **DEM variant**: `--isolated --yade` (and `--prefix DIR --yade`) copies the coupling sources from `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` into `<prefix>/foam-yade/`, compiles `libMeshTree`, `libYadeComm` and `libYadeFoam` there into `<prefix>/lib` with a `.foamyade-coupling.json` stamp, and compiles PGF against those headers. The trunk is only read, so it needs no `lnInclude` and no prior build. Setting `PGF_YADE_LIBDIR` instead copies prebuilt libraries (and their stamp) from that directory. The in-tree objects are per variant: switching a checkout between `--yade` and the plain build needs `./pgf exec --bare -- ./build.sh clean --all --isolated` first.
- Every build prints where it installs. OpenFOAM v2406 embeds no RPATH, so a prefix works anywhere as long as its `lib/` is on `LD_LIBRARY_PATH`.

### Choosing a build

Each command uses one build. It comes from, later winning: **auto** (the build of this checkout's sources as they are now), `PGF_BUILD` in the checkout's `.pgf-env.conf`, the terminal (`PGF_BUILD` in the environment, which `pgf use` sets), and `--use SEL` on the command line. `PGF_BUILD` is refused in the user file. `SEL` is a key or unique key prefix, a commit (the newest build made from it), a worktree name or path (its last build), a prefix path, or `auto`. `--prefix DIR` uses that complete prefix as it is (also one built with `build.sh --prefix` outside the store) and skips the staleness check.

`pgf use SEL` prints the line that sets the terminal layer; apply it with `eval "$(pgf use SEL)"`. Inside `pgf shell` a `pgf` function does the `eval`; the shell itself keeps the build it started with, so later `pgf exec` / `pgf run` calls follow the new choice.

A selected build that is not this checkout's sources is **stale**: `pgf exec` and `pgf shell` warn, `pgf run` refuses. `pgf run --build` builds this checkout first (it cannot be combined with a selected build), and `pgf run --allow-stale` runs a stale build anyway. The banner names the layer the build came from.

`pgf builds [--json]` lists every build (variant, age, size, the checkout that built it, `[in use]`, `[building]`); `pgf ls` adds the registered workspaces. Every `pgf exec`, `shell` and `run` holds a shared `flock` on `$PGF_BUILD_ROOT/<key>.use.lock` while it runs. `pgf gc [--keep N] [--yes] [--dry-run] [--json]` removes builds that are not this checkout's, not a registered workspace's last build, not in use and not being built, keeping the `N` newest of the rest (default 2). Without `--yes` it asks on a terminal and refuses (exit 2) without one. `pgf-test --gc` also skips builds in use.

`pgf status [--json]` ends with this checkout's two keys (`yade0`, `yade1`) and whether each is built; `pgf status PATH` asks another checkout. `pgf ls [--json]` lists every prefix under the build root with its variant, build time, size, the checkout that built it and the checkouts whose last build it is; `*` marks this checkout's.

`pgf build [--yade] [build.sh options]` is `pgf exec --bare [--yade] -- ./build.sh --isolated [--yade] [options]`.

`pgf exec [--prefix DIR | --for-tree | --bare] [--yade] [--mpi loopback] -- <command>` runs a command in a clean environment. It drops the caller's environment except `HOME USER LANG TERM TMPDIR WM_PROJECT_DIR XDG_CONFIG_HOME PGF_* YADE_*` and sources `$PGF_FOAM_BASHRC` (default `$WM_PROJECT_DIR/etc/bashrc`) with `set +eu`. Then it points `WM_PROJECT_USER_DIR`, `FOAM_USER_APPBIN` and `FOAM_USER_LIBBIN` at the prefix and removes every `PATH` / `LD_LIBRARY_PATH` entry under the user dir `bashrc` chose. It also sets `UCX_VFS_ENABLE=n UCX_LOG_LEVEL=fatal`, `PGF_ISOLATED_ONLY=1` (`build.sh` then refuses to build without `--prefix` / `--isolated`) and `TMPDIR=$PGF_RUN_ROOT/tmp`. It checks that `porousGasificationFoam` resolves into the prefix and prints a one-line banner on stderr.

- `--for-tree` (default) uses this checkout's `--isolated` prefix and exits 2 if it is not built. `--bare` uses no prefix; use it to run `build.sh`.
- `--yade` adds `YADE_TRUNK` (default `submodules/Foam-Yade`), a `yade` on `PATH` (from `YADE_BIN` or the caller's `PATH`) and `PYTHONPATH` (`YADE_PYTHONPATH`, or derived from the install). If `/tmp` is read-only, it also preloads a small shim that moves Yade's `/tmp/yade-XXXXXX` into `$TMPDIR`.
- `--mpi loopback` restricts Open MPI to `self,vader,tcp` on `lo`. The DEM coupling connects Yade's MPI job to the solver it spawns over TCP, and Open MPI leaves `lo` out by default, so on a host or sandbox whose only interface is `lo` the run hangs with `help-mca-bml-r2.txt / unreachable proc`; on a host with docker/libvirt bridges it can hang probing them. The default, `PGF_MPI=auto`, applies loopback when a `docker*`, `virbr*`, `br-*` or `veth*` interface is up or when not exactly one other interface is up, and says so in the banner (`MPI loopback (auto: …)`). `--mpi default` (or `PGF_MPI=default`) leaves Open MPI alone. Runs are single-host; `auto` does not handle multi-host jobs.
- `pgf doctor` prints the settings and read-only checks, each `OK`, `WARN` or `FAIL` with a fix: the bashrc and OpenFOAM version, this tree's prefix, yade (binary, version against the pin, `MPI` in its features), the coupling stamp, the MPI decision and the interfaces it saw, and whether `pgf` is on `PATH`. It exits 1 on any `FAIL`.

### Remembered settings

`pgf` reads its settings (`PGF_FOAM_BASHRC`, `YADE_TRUNK`, `YADE_BIN`, `YADE_PYTHONPATH`, `PGF_YADE_LIBDIR`, `PGF_BUILD_ROOT`, `PGF_RUN_ROOT`, `PGF_YADE=0|1`, `PGF_MPI=default|loopback|auto`, `PGF_BUILD` (see [Choosing a build](#choosing-a-build); not in the user file)) from layers; a later layer wins:

1. built-in default;
2. user file `${XDG_CONFIG_HOME:-~/.config}/pgf/env.conf`;
3. checkout file `.pgf-env.conf` at the tree root (git-ignored, so each worktree has its own);
4. the caller's environment;
5. `--set KEY=VALUE` (or `--foam-bashrc FILE`, `--yade` / `--no-yade`, `--mpi …`).

The files hold `KEY=value` lines and `#` comments. They are parsed, not sourced: an unknown key or a malformed line exits 2 naming `file:line`, and only the keys above are accepted, so a file cannot set `PATH` or run code. `PGF_YADE=1` makes `--yade` the default; `PGF_MPI=loopback` does what `--mpi loopback` does. `pgf status` and `pgf config` print every effective value and the layer (and `file:line`) it came from; `pgf config --json` gives the same as JSON:

```bash
printf 'PGF_FOAM_BASHRC=%s\n' ~/OpenFOAM/OpenFOAM-v2406/etc/bashrc >> ~/.config/pgf/env.conf
./pgf status
```

### `setup`, `config`, `shell`, `run`

```bash
./pgf setup --yes                          # detect OpenFOAM and yade, write ~/.config/pgf/env.conf, link ~/.local/bin/pgf
./pgf setup --checkout --yes --no-link --foam-bashrc FILE   # or this worktree's .pgf-env.conf, no link
./pgf setup --link-only                    # only ~/.local/bin/pgf -> this checkout's pgf
./pgf config get PGF_MPI                   # one resolved value (exit 1 if unset)
./pgf config set PGF_MPI=loopback --checkout   # edit the user (default) or checkout file
./pgf config unset PGF_MPI --checkout
./pgf shell                                # interactive bash in the env, prompt (pgf:<worktree>@<key>)
./pgf shell -c 'command -v porousGasificationFoam'
./pgf run tutorials/cases/<case> [Allrun | Allclean | script] [args]
```

- `setup` looks for `$WM_PROJECT_DIR/etc/bashrc`, `~/OpenFOAM/*/etc/bashrc`, `/opt/openfoam*` and `/usr/lib/openfoam/*` (v2406 first), and for `yade` or a suffixed `yade-*` on your `PATH`. It asks when several candidates exist and a terminal is attached, unless `--yes`. Values given as `--foam-bashrc`, `--yade-bin`, `--yade-trunk` or `--set` win, and keys already in the file are kept. `--checkout` also records `submodules/Foam-Yade` as `YADE_TRUNK` when it is checked out.
- `shell` takes the same options as `exec` (`--yade`, `--prefix`, `--bare`, …). Nested `pgf` calls inside it skip the `env -i` step.
- `run` changes into the case directory (resolved from your current directory) before running, so it works from anywhere. A case whose `constant/yadeProperties` says `active true` gets `--yade` unless `--no-yade` is given or `PGF_YADE` is set in a file or the environment.
- After the script ends, `run` writes a run record to `<case>/.pgf-run.json`: the command, start and end times, exit status, the build (key, prefix, variant, commit, and the layer it was selected by) and, for a DEM run, the yade binary, its commit and the Foam-Yade pin. The same record is appended as one line to `$PGF_BUILD_ROOT/.pgf/runs.jsonl` when the build root is writable. `run` exits with the script's status, and still writes the record after Ctrl-C.
- The link step refuses to replace a link to another checkout without `--force`. With several worktrees, the link picks one; call the others by path.

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
2. **Coupling libraries** — three `wmake` libraries under `$YADE_TRUNK/pkg/openfoam/coupling/FoamYade` (`libMeshTree`, `libYadeComm`, `libYadeFoam`, in that order). `pgf build --yade` compiles them into the build (see [Isolated builds](#isolated-builds-and-pgf)); nothing else is needed. For a non-prefix build, or to prebuild them for `PGF_YADE_LIBDIR`, build and stamp them with

   ```bash
   ./pgf exec --bare --yade -- utilities/pgf/buildCoupling.sh --libdir DIR   # default $PGF_YADE_LIBDIR
   ```

   It writes `.foamyade-coupling.json` next to the libraries: the Foam-Yade commit, the `YADE_TRUNK` used and the coupling-header hash. `build.sh --yade` compares that hash with the headers it is about to compile against and stops before `wmake` on a mismatch, printing the command above; `pgf --yade` does the same for the copy in the prefix. Both warn when `YADE_TRUNK`'s `HEAD` differs from the submodule pin, or when the libraries carry no stamp (built by hand). With `PGF_YADE_LIBDIR` set, an `--isolated --yade` build copies the libraries and the stamp into its prefix from there.
   As a last line, `porousGasificationMedia/DEM/foamYadeApiCheck.H` fails the compile by name when Foam-Yade's `FOAMYADE_COUPLING_API` differs from the one PGF needs; a Foam-Yade older than that macro is not caught there.
3. **PGF** — `./pgf build --yade` (or `./build.sh build --yade` into the user dir).

`pgf exec --yade` (and `run`, `shell`, `doctor`) checks the yade binary (`YADE_BIN`, or `yade` on `PATH`) against the pin. It reads the Foam-Yade commit from `yade -v` (`git-XXXXXXX`; cached per binary under `~/.cache/pgf`). The pinned commit is OK. Another commit whose coupling headers hash the same as `YADE_TRUNK`'s warns. Another coupling API fails (exit 2). A commit the trunk's git does not know, or no commit at all, warns. `PGF_YADE` stays the 0/1 switch for the DEM variant; the binary is `YADE_BIN`.

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
