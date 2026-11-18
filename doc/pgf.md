# The pgf tool

`pgf` is PGF's command-line tool. Its job is a reproducible, well-defined state across different PGF snapshots and yade versions. `pgf` builds each checkout into its own build, keyed by sources, variant, OpenFOAM and compiler versions. It runs every case and test in a clean environment with appropriate build, recording which one it used.

`pgf` manages three separate areas:

| | Managed area | Commands (`pgf …`) |
|---|---|---|
| [Settings](#settings) | which OpenFOAM, yade and directories to use | `setup`, `config`, `doctor` |
| [Builds](#builds) | a build of its own for each checkout | `build`, `builds`, `use`, `gc` |
| [Runs](#runs) | cases and tests, run with a well defined build | `copy`, `run`, `shell`, `view`, `test` |

`pgf status` summarizes currently active pgf state.

`pgf` works on the checkout it runs from: the one `pgf setup` put on your `PATH`, or another worktree when you call its `./pgf`.

To set pgf up and run a first case, follow the [quick start](../README.md#quick-start).

## Settings

Settings say which OpenFOAM and yade to use, where builds, scratch and case copies go, and, per checkout, which build to use. They live in two files of `KEY=value` lines:

- **User file:** `~/.config/pgf/env.conf`, for every checkout. `pgf setup` writes it.
- **Checkout file:** `.pgf.conf` at the top of the checkout pgf runs from, git-ignored, so each worktree has its own.

### Setting up

`pgf setup` is the first command you run, once per machine. It finds OpenFOAM (and yade, if you have it), saves where they are in the user file, links `~/.local/bin/pgf` to the checkout and installs Tab completion. Run it again after installing another OpenFOAM or yade, or to put another worktree's `pgf` on your `PATH`.

```bash
./pgf setup         # asks, then saves and links
./pgf setup --yes   # takes what it finds

# use the OpenFOAM installation whose etc/bashrc you name,
# instead of the one pgf setup finds by itself
./pgf setup --foam-bashrc /opt/OpenFOAM/OpenFOAM-v2406/etc/bashrc
```

To make the `pgf` command run another worktree's pgf, run `./pgf setup --link-only --force` in that worktree. It only repoints the `~/.local/bin/pgf` link, and `--force` replaces a link to another checkout.

All options, and where setup looks: `pgf help setup`.

### Viewing and changing settings

```bash
pgf config get PGF_MPI                       # one value
pgf config set PGF_MPI=loopback --checkout   # only for the checkout pgf runs from
pgf config                                   # every value and where it came from
```

The checkout file wins over the user file, and the environment or a `--set KEY=VALUE` option wins over both. Every key, the files and how the layers combine: `pgf help config`.

### Checking the setup

`pgf doctor` answers one question: can you build and run cases, and if not, what do you fix? Run it after `pgf setup`, after changing a setting, or when a build or a run fails. It only reads; it changes nothing.

```bash
pgf doctor          # check everything; DEM problems are only warnings
pgf doctor --yade   # you need DEM: its problems are failures
```

It checks, in groups: OpenFOAM, the builds of the checkout pgf runs from, yade and the DEM coupling, MPI, and whether `pgf` is on your `PATH`. Each check is one `OK`, `WARN` or `FAIL` line, and under each problem a `fix:` line says what to run. A verdict closes the output. For example, with a yade built from the wrong Foam-Yade commit (shortened and wrapped):

```text
yade and DEM
  OK   yade ~/.local/bin/yade (Yade version: …), with MPI
  WARN yade … was built from Foam-Yade d5c33e0, whose coupling API
       differs from the pinned 287187f703: DEM runs would exchange wrong data
       fix: rebuild yade from Foam-Yade 287187f703 (doc/build.md, …),
       then pgf config set YADE_BIN=<that yade>

verdict: ready for non-DEM runs; DEM needs: a yade built from the Foam-Yade pin
```

It exits 1 if any check fails, so scripts can use it too. Every check: `pgf help doctor`.

## Builds

A build is a compiled solver with its libraries and utilities, in a directory of its own under `$PGF_BUILD_ROOT` (by default `~/OpenFOAM/<user>-v2406/pgf-builds`). Its key comes from the sources, the variant (with or without DEM), OpenFOAM and the compiler, so each checkout state gets its own build, reused while the key matches and never overwritten. [build.md](build.md) covers building.

```bash
pgf build                  # build the checkout pgf runs from; reused if unchanged
pgf build --yade           # the DEM variant, for cases with constant/yadeProperties
pgf builds                 # every build, the checkout that made it, the existing checkouts whose last build it is
pgf gc                     # remove old builds no checkout uses
```

By default every command uses the build of the checkout pgf runs from. To use another, say an older commit's or another worktree's, select it: `--use SEL` for one command, or `eval "$(pgf use SEL)"` for the terminal. `pgf run` refuses a build of other sources than that checkout's unless you add `--allow-stale`. Details: `pgf help build-selection`.

## Runs

A run is a case run with a build, in a clean environment. It leaves things in three places:

- **Your cases:** `$PGF_CASE_ROOT/<branch>/<case>`, by default OpenFOAM's run dir, `~/OpenFOAM/<user>-v2406/run`.
- **Scratch:** `$PGF_RUN_ROOT`, by default `${TMPDIR:-/tmp}/pgf-runs`, for test runs and temporary files. Under `/tmp` it usually does not survive a reboot.
- **Records:** `~/.local/state/pgf/runs`, a line per run and the last result of each test tier.

```bash
pgf copy --list                       # the tutorials
pgf copy charOnlyMoveCases/serial     # copy one to $PGF_CASE_ROOT/<branch>/charOnlyMoveCases/serial
pgf run <case>                        # run its Allrun; or name Allclean or another script
pgf shell                             # an interactive shell in the same environment
pgf view <case>                       # open the results in ParaView
pgf test <tier>                       # run a regression tier
```

- **`copy`** copies the whole tutorial and records the checkout and commit it came from, so `pgf run` uses that checkout's build. An existing copy is kept unless `--force`. Run copies, not the tutorials in the checkout.
- **`run`** works from any directory. It runs the script in the case directory and writes `<case>/.pgf-run.json`: the command, its exit status and the build it used. Details: `pgf help run-record`.
- **`shell`** gives you OpenFOAM and the build on `PATH`, and little else from your own environment. `pgf shell -- <command>` runs one command there. Details: `pgf help environment`.
- **`view`** opens the case in ParaView ([viewing a case](paraview.md)). **`test`** runs a regression tier ([regression testing](regression-testing.md)).

## Help and output

`pgf help` is the reference: it lists the commands and the help topics, `pgf help <command>` (or `pgf <command> --help`) explains one command, and `pgf help <topic>` one topic (`build-selection`, `run-record`, `environment`). `pgf help --all` shows every page in one go, through `less` on a terminal, so you can search it; piped, it prints plain text. `pgf tools` describes the scripts at the top of the checkout. Every command behaves the same from any working directory.

Status lines read `OK`, `WARN`, `FAIL`, `SKIP`, `INFO` (`PASS`/`FAIL` for tests). `setup`, `build`, `copy`, `run` and `test` end with a `next:` block that suggests the command to run next. Colour is on when both stdout and stderr are a terminal: `--color=auto|always|never` (or `PGF_COLOR`) and `NO_COLOR` control it, and log files never get colour codes.

## Related pages

- [Building](build.md): the build store, the DEM coupling, cleaning.
- [Apptainer images](containers.md): `pgf pack`, an image of one commit for a server.
- [Setting up your own case](case-setup.md): required fields, input files, tutorial cases, tips.
- [All documentation](README.md)
