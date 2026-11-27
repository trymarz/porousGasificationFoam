# pgf reference

Every page of `pgf help`, as plain text. **Generated** by `utilities/gen-pgf-reference.sh` from the pgf in this checkout; do not edit by hand. A tooling test (`applications/test/tooling/test_pgfCommands.sh`) fails when this page is stale. How pgf works, and when to use which command: [the pgf tool](pgf.md).

- [pgf](#pgf)
- [pgf help setup](#pgf-help-setup)
- [pgf help config](#pgf-help-config)
- [pgf help doctor](#pgf-help-doctor)
- [pgf help build](#pgf-help-build)
- [pgf help builds](#pgf-help-builds)
- [pgf help use](#pgf-help-use)
- [pgf help gc](#pgf-help-gc)
- [pgf help copy](#pgf-help-copy)
- [pgf help run](#pgf-help-run)
- [pgf help dem](#pgf-help-dem)
- [pgf help view](#pgf-help-view)
- [pgf help shell](#pgf-help-shell)
- [pgf help test](#pgf-help-test)
- [pgf help status](#pgf-help-status)
- [pgf help pack](#pgf-help-pack)
- [pgf help tools](#pgf-help-tools)
- [pgf help help](#pgf-help-help)
- [pgf help build-selection](#pgf-help-build-selection)
- [pgf help run-record](#pgf-help-run-record)
- [pgf help environment](#pgf-help-environment)

## pgf

```text
pgf: settings, builds, runs and tests for porousGasificationFoam

Usage:
  pgf <command> [options]

Description:
  pgf is the one entry point to porousGasificationFoam. It remembers where
  OpenFOAM and yade live, builds each checkout into its own prefix, and runs
  cases and test tiers in a clean environment with that build, so nothing
  depends on what your shell has sourced.
  Settings come from, later winning: the default, ~/.config/pgf/env.conf,
  <checkout>/.pgf.conf, the environment, the command line (pgf help config).
  If you only need the OpenFOAM-style entry point, ./Allwmake and ./Allwclean
  build and clean through pgf.

Settings Commands:
  setup            find OpenFOAM and yade, save them, put pgf on PATH
  config           show every setting with its layer, or change one
  doctor           check that pgf can build and run, with a fix for each problem

Build Commands:
  build            build the checkout (libraries and solver)
  builds           every build, and the checkouts that made them
  use              choose a build for this terminal (with eval)
  gc               remove old unused builds

Run Commands:
  copy             copy a tutorial case out of the checkout, ready to run
  run              run a case script and record the run
  dem              coupled Yade + solver runs (DEM cases)
  view             look at a case's results in ParaView (pgfView)
  shell            a shell, or one command, in pgf's clean environment
  test             run a test tier against the checkout's build

Other Commands:
  status           what pgf uses and where: files, the build, your case copies
  pack             an Apptainer image of a commit
  tools            every tool at the checkout root: what, why, use instead
  help             help about a command or a topic

  Old names still work: where (status), ls (builds), exec (shell --).

Help Topics:
  build-selection  which build a command uses, build keys, stale builds
  run-record       what pgf run writes about each run, and where
  environment      the clean environment of pgf shell, run and build

Options:
  --color WHEN  colour: auto (when output goes to a terminal and NO_COLOR is unset), always, never (default: auto)
  -h, --help    this help, or a command's

Examples:
  $ ./pgf setup              # find OpenFOAM, save it, put pgf on PATH
  $ pgf build                # build the checkout pgf runs from
  $ pgf copy solidAdvection  # copy a tutorial out of the checkout; prints its path
  $ pgf run ~/OpenFOAM/$USER-v2606/run/main/solidAdvection  # run that copy
  $ pgf view ~/OpenFOAM/$USER-v2606/run/main/solidAdvection  # look at the results
  $ pgf doctor               # check the setup, with fixes

Use "pgf help <command>" or "pgf <command> --help" for more about a command,
"pgf help <topic>" for a topic, "pgf help --all" for every page.
```

## pgf help setup

```text
pgf setup: find OpenFOAM and yade, save them, put pgf on PATH

Usage:
  pgf setup [--yes] [--user | --checkout] [--no-link | --link-only] [--force] [--no-completion]
              [--foam-bashrc FILE] [--yade-bin FILE] [--yade-trunk DIR] [--set KEY=VALUE]

Description:
  pgf runs every build and case in a clean environment, so it has to know where
  OpenFOAM (and, for DEM cases, yade) lives. setup finds them, saves them in a
  settings file that every pgf call reads, links ~/.local/bin/pgf to the
  checkout pgf runs from and installs bash completion. It prints one line per step as it goes,
  then what it saved where, then what to do next.

  On a terminal it asks when there is a choice; --yes takes the detected choices
  without asking. Rerun it any time: it keeps the values you set and says what
  changed. Change a value later with pgf config set KEY=VALUE; what each setting
  means and how the layers combine: pgf help config.

  It looks for OpenFOAM at $WM_PROJECT_DIR/etc/bashrc, ~/OpenFOAM/*/etc/bashrc,
  /opt/openfoam* and /usr/lib/openfoam/* (v2606 first), and for yade or a
  suffixed yade-* on your PATH. Values given with --foam-bashrc, --yade-bin,
  --yade-trunk or --set win, and keys already in the file are kept. --checkout
  also records submodules/yade-pgf as YADE_TRUNK when it is checked out.

  The link step refuses to replace a link to another checkout without --force.
  With several worktrees the link picks one; call the others by path.
  --link-only does only the link: no settings, no completion.

Options:
  -y, --yes           do not ask: take what was detected, install completion
  --user              save to ~/.config/pgf/env.conf, for every checkout (default)
  --checkout          save to <checkout>/.pgf.conf, for that checkout only
  --no-link           do not link ~/.local/bin/pgf
  --link-only         only link ~/.local/bin/pgf to the checkout pgf runs from
  --force             replace a link that points at another checkout
  --no-completion     do not install bash completion
  --foam-bashrc FILE  this OpenFOAM etc/bashrc instead of a detected one
  --yade-bin FILE     this yade binary instead of a detected one
  --yade-trunk DIR    this yade-pgf source tree
  --set KEY=VALUE     save any setting (keys: pgf help config)

Examples:
  $ ./pgf setup                      # ask, then save and link
  $ ./pgf setup --yes                # take the detected choices
  $ ./pgf setup --foam-bashrc ~/OpenFOAM/OpenFOAM-v2606/etc/bashrc
  $ ./pgf setup --link-only --force  # make pgf on PATH the ./pgf you call

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help config

```text
pgf config: show every setting with its layer, or change one

Usage:
  pgf config [get KEY | set KEY=VALUE | unset KEY] [--user | --checkout] [--json]

Description:
  Each setting comes from, later winning: the default, ~/.config/pgf/env.conf
  (user layer), <checkout>/.pgf.conf (checkout layer), the environment, the
  command line. Without a verb, config prints every setting and its layer
  (file:line for a file, [default] for a built-in default); set and unset edit
  the user file, or the checkout file with --checkout. get prints one resolved
  value and exits 1 when the key is unset.

  The user file is ${XDG_CONFIG_HOME:-~/.config}/pgf/env.conf. The checkout file
  is .pgf.conf at the checkout root; the old name .pgf-env.conf is still read,
  with a note, while no .pgf.conf exists, and the next set --checkout or
  pgf setup --checkout writes .pgf.conf. The files hold KEY=value lines and #
  comments. They are parsed, not sourced: an unknown key or a malformed line
  exits 2 naming file:line, so a file cannot set PATH or run code. PGF_BUILD is
  refused in the user file (pgf help build-selection).

  On the command line, --set KEY=VALUE overrides one key, and --foam-bashrc,
  --yade / --no-yade and --mpi set theirs. PGF_YADE=1 makes --yade the default;
  PGF_MPI=loopback does what --mpi loopback does (pgf help environment).

Options:
  --user      edit ~/.config/pgf/env.conf (default)
  --checkout  edit <checkout>/.pgf.conf
  --json      print the settings and layers as JSON

Settings:
  PGF_FOAM_BASHRC  OpenFOAM etc/bashrc sourced before every build and run
  YADE_TRUNK       Yade source tree (PgfCoupling, mpy.py), for the pin checks; default: the submodule
  YADE_BIN         yade binary for DEM (--yade) runs
  YADE_PYTHONPATH  yade's Python modules; default: found beside YADE_BIN
  PGF_BUILD_ROOT   the build store; default: $WM_PROJECT_USER_DIR/pgf-builds
  PGF_RUN_ROOT     scratch: test runs and temporary files; default: $TMPDIR/pgf-runs-$USER
  PGF_CASE_ROOT    where pgf copy puts cases, as <branch>/<case>; default: $WM_PROJECT_USER_DIR/run
  PGF_YADE         1: use the DEM (--yade) variant by default
  PGF_MPI          Open MPI networking: auto, loopback (lo only) or default
  PGF_BUILD        the build to use (pgf use); default auto: the build of the checkout pgf runs from

Examples:
  $ pgf config
  $ pgf config set PGF_FOAM_BASHRC=~/OpenFOAM/OpenFOAM-v2606/etc/bashrc
  $ pgf config set PGF_MPI=loopback --checkout  # only the checkout pgf runs from
  $ pgf config get YADE_BIN

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help doctor

```text
pgf doctor: check that pgf can build and run, with a fix for each problem

Usage:
  pgf doctor [options]

Description:
  Read-only checks in groups: OpenFOAM, the builds of the checkout pgf runs
  from, yade and DEM, MPI, ParaView, and pgf on PATH. Each check is one OK, WARN or FAIL line; under a WARN
  or FAIL, the detail and the command that fixes it. It ends with a one-line
  verdict and exits 1 when a check fails. The settings themselves: pgf config.

  It checks the OpenFOAM bashrc and version, the build of the checkout pgf runs
  from, yade (the binary, its version against the yade-pgf pin, MPI among its
  features), the coupling stamp, the MPI decision and the interfaces it saw, whether
  the ParaView on your PATH has Python for pgf view, and whether pgf is on PATH.

Options:
  --use SEL           the build: key or key prefix, commit, worktree, prefix path or auto (default: auto)
  --prefix DIR        use this complete prefix as it is; no staleness check
  --for-tree          same as --use auto
  --bare              no build: OpenFOAM only
  --yade              the DEM variant, plus yade, YADE_TRUNK and PYTHONPATH (default: PGF_YADE)
  --no-yade           the plain variant even when PGF_YADE=1
  --mpi MODE          Open MPI on lo only (loopback), untouched (default), or auto (default: PGF_MPI, auto)
  --set KEY=VALUE     override one setting for this call
  --foam-bashrc FILE  same as --set PGF_FOAM_BASHRC=FILE
  --yade-bin FILE     same as --set YADE_BIN=FILE
  --yade-trunk DIR    same as --set YADE_TRUNK=DIR

Examples:
  $ pgf doctor
  $ pgf doctor --yade  # also check the DEM build

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help build

```text
pgf build: build the checkout (libraries and solver)

Usage:
  pgf build [--yade] [-v] [build.sh options]

Description:
  Builds the checkout pgf runs from into the build store, in a prefix named by
  a key made of the sources, the variant, the OpenFOAM version and the compiler. A prefix with
  the same key is reused without compiling. wmake's output goes to
  <prefix>/build.log; the screen shows one line per target. ./Allwmake runs this.
  Switching between the normal and DEM variants needs no clean: pgf build drops the
  other variant's objects of the targets that differ and says so. After editing the
  sources, run pgf build again; wmake inside pgf shell is refused.

Options:
  --yade          the DEM variant (WITH_YADE=1)
  -v, --verbose   stream wmake's output instead of logging it
  --libs-only     only the libraries
  --dry-run       show what would be built; build nothing
  --print-prefix  print the prefix the checkout builds into
  clean           clean the in-tree cache (lnInclude/, Make/<WM_OPTIONS>/); prefixes stay

Examples:
  $ pgf build
  $ pgf build --yade               # the DEM variant
  $ utilities/pgf/build.sh --help  # every build.sh option

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help builds

```text
pgf builds: every build, and the checkouts that made them

Usage:
  pgf builds [--json]

Description:
  Lists the checkouts that built into the build store and still exist (and how
  many no longer do; pgf gc forgets them), then every build: key, variant, age,
  size, the checkout that built it, the existing checkouts whose last build it is
  (last build of ...), [in use] and [building]. A legend and the total count and
  size close the list: * marks the builds of the current sources of the checkout
  pgf runs from, and (incomplete) a build that stopped before it finished, which
  gc removes. Old name: ls. What a build and its key are: pgf help build-selection.

Options:
  --json  as JSON

Examples:
  $ pgf builds

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help use

```text
pgf use: choose a build for this terminal (with eval)

Usage:
  eval "$(pgf use <key|commit|worktree|prefix|auto> [--yade])"

Description:
  Prints the shell line that makes a build this terminal's: wrap it in eval.
  Without eval it only prints that line and changes nothing. Inside pgf shell,
  plain pgf use does the eval. auto goes back to the build of the sources of the
  checkout pgf runs from.

  Run without eval on a terminal, it says it only printed the line; under eval
  it says the terminal now uses the build. Either way it notes when the build is
  not of the checkout's current sources. Inside pgf shell, the shell itself keeps
  the build it started with; later pgf shell and pgf run calls follow the new
  choice. What SEL can be: pgf help build-selection.

Options:
  --yade     pick from the DEM builds
  --no-yade  pick from the plain builds

Examples:
  $ eval "$(pgf use a2055)"  # a key prefix
  $ eval "$(pgf use auto)"

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help gc

```text
pgf gc: remove old unused builds

Usage:
  pgf gc [--keep N] [--yes] [--dry-run] [--json]

Description:
  Removes builds that no existing checkout last built, that are not in use or
  being built, beyond the newest N, and forgets checkouts that no longer exist.
  The current builds of the checkout pgf runs from always stay. Each kept build
  gives its reason (a build of <name>'s current sources, chosen with pgf use or
  PGF_BUILD, in use, building, last build of <checkout>, one of the N newest
  others, --keep), and each removed one says if it is incomplete.

  It ends with a summary: N build(s) to remove, SIZE; M kept, plus the checkouts
  that no longer exist, whose records a real gc forgets. --dry-run ends with
  nothing removed (--dry-run). Without --yes it asks on a terminal, and refuses
  (exit 2) without one. In use means a pgf shell or pgf run holds the build's
  lock (pgf help environment); pgf test --gc skips those builds too.

Options:
  --keep N   keep the N newest (default: 2)
  --yes      remove without asking
  --dry-run  only list what would go
  --json     as JSON

Examples:
  $ pgf gc --dry-run

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help copy

```text
pgf copy: copy a tutorial case out of the checkout, ready to run

Usage:
  pgf copy <tutorial> [NAME] [--force] | pgf copy --list

Description:
  Copies tutorials/cases/<tutorial>, the whole case with linked files resolved
  (cp -rL), to $PGF_CASE_ROOT/<branch>/<tutorial>, or <branch>/NAME. <branch> is
  the branch of the checkout pgf runs from, with / as -, or its short commit.
  PGF_CASE_ROOT defaults to OpenFOAM's run dir, $WM_PROJECT_USER_DIR/run
  (~/OpenFOAM/<user>-v2606/run), not pgf's scratch $FOAM_RUN.

  The copy records the checkout and commit it came from: pgf run then uses that
  checkout's build, and warns when the branch has moved on since. The record is
  <copy>/.pgf-source.json. An existing copy is kept unless --force. pgf status
  lists your copies per branch.

Options:
  --list   list the tutorials, one line each
  --force  replace an existing copy

Examples:
  $ pgf copy --list
  $ pgf copy charOnlyMoveCases/serial  # prints the copy's path and the pgf run line
  $ pgf copy canonical/pyrolysis pyrolysis-2  # a second copy under another name

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help run

```text
pgf run: run a case script and record the run

Usage:
  pgf run [options] [--build] [--allow-stale] <caseDir> [Allrun | Allclean | script] [args ...]

Description:
  Changes into caseDir and runs the script (default Allrun) in the clean
  environment, then writes <caseDir>/.pgf-run.json and says how it went. It works
  from any directory; caseDir is resolved from the current one. A case whose
  constant/yadeProperties says active true gets --yade, unless --no-yade is given
  or PGF_YADE is set in a file or the environment. Run a copy of a tutorial
  (pgf copy), not the tutorial in the checkout: a copy runs with the build of the
  checkout it came from. A stale build is refused; the refusal names the layer
  that chose it and the fix. run exits with the script's status.

  The run record: pgf help run-record. The environment: pgf help environment.
  Choosing a build and staleness: pgf help build-selection.

Options:
  --build             build the checkout pgf runs from first
  --allow-stale       run with a build of other sources
  --use SEL           the build: key or key prefix, commit, worktree, prefix path or auto (default: auto)
  --prefix DIR        use this complete prefix as it is; no staleness check
  --for-tree          same as --use auto
  --bare              no build: OpenFOAM only
  --yade              the DEM variant, plus yade, YADE_TRUNK and PYTHONPATH (default: PGF_YADE)
  --no-yade           the plain variant even when PGF_YADE=1
  --mpi MODE          Open MPI on lo only (loopback), untouched (default), or auto (default: PGF_MPI, auto)
  --set KEY=VALUE     override one setting for this call
  --foam-bashrc FILE  same as --set PGF_FOAM_BASHRC=FILE
  --yade-bin FILE     same as --set YADE_BIN=FILE
  --yade-trunk DIR    same as --set YADE_TRUNK=DIR

Examples:
  $ pgf copy solidAdvection  # prints where the copy went
  $ pgf run ~/OpenFOAM/$USER-v2606/run/main/solidAdvection  # the copy of a checkout on main
  $ pgf run ~/OpenFOAM/$USER-v2606/run/main/solidAdvection Allclean

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help dem

```text
pgf dem: coupled Yade + solver runs (DEM cases)

Usage:
  pgf dem run [-ndem N] [-nfoam M] [-solver NAME] [--stall-timeout S] SCRIPT.py [yade args ...]

Description:
  pgf dem run starts a coupled DEM run from the case directory: Yade and the
  solver in one mpirun of N+M ranks, joined by PgfDemBridge. The DEM tutorials'
  Allrun call it; run those through pgf run, whose environment it keeps. The case
  must be decomposed into M processor* directories (processor0 when M is 1).
  Open MPI gets --oversubscribe; PGF_MPIRUN_FLAGS adds mpirun options. Exit 2 on
  a usage or case error, otherwise mpirun's status.

  A stall watchdog runs mpirun in its own process group. When the run prints
  nothing for S seconds, it TERMs that group, KILLs it 10 s later
  (PGF_DEM_KILL_GRACE), prints the last output line and exits 124, the TIMEOUT
  of pgf test. A run that keeps printing is never stopped. stdout and stderr
  are merged. INT, TERM and HUP are passed on to the group.
  tutorials/cases/DEM_common/pgfDemRun is its old name.

Options:
  -ndem N            Yade ranks, mpy's master included (default: 2)
  -nfoam M           OpenFOAM ranks (default: numberOfSubdomains)
  -solver NAME       the solver (default: porousGasificationFoam)
  --stall-timeout S  stop a run silent for S seconds; 0: never (default: PGF_DEM_STALL_TIMEOUT, 600)

Examples:
  $ pgf dem run -ndem 2 MPI_lambda.py  # in an Allrun, from the case directory

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help view

```text
pgf view: look at a case's results in ParaView (pgfView)

Usage:
  pgf view <caseDir> [--png DIR [--time T] [--size WxH] [--xvfb]] [--compare OTHER] [--palette NAME]

Description:
  Opens the case in ParaView with the views its system/paraviewViews.py asks for,
  one layout tab each, or with --png renders them to PNGs without a screen. The
  GUI keeps your ParaView palette; --png uses WhiteBackground. ParaView comes
  from your PATH, not pgf's clean environment. This is
  applications/utilities/paraview/pgfView; more in doc/paraview.md.

Options:
  --png DIR        write DIR/<view>.png instead of opening the GUI
  --time T         with --png: this time (default: the last mesh time)
  --size WxH       with --png: the image size (default: 1000x750)
  --xvfb           with --png: render under xvfb-run (also when DISPLAY is unset)
  --compare OTHER  the case, OTHER and their difference, side by side
  --palette NAME   a ParaView palette, e.g. BlackBackground

Examples:
  $ pgf view ~/OpenFOAM/$USER-v2606/run/main/solidAdvection
  $ pgf view <caseDir> --png out/  # headless: out/<view>.png

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help shell

```text
pgf shell: a shell, or one command, in pgf's clean environment

Usage:
  pgf shell [options] [-c COMMAND | -- COMMAND ...]

Description:
  Starts bash with the caller's environment dropped (a small allowlist kept),
  OpenFOAM's bashrc sourced and the user dirs pointed at the build, with the
  prompt (pgf:<worktree>@<key>); pgf calls inside it keep that environment.
  With -- COMMAND it runs that command instead (old name: pgf exec). A build
  not made from the sources of the checkout pgf runs from is stale: shell warns.
  --for-tree exits 2 when that checkout is not built. What the environment holds
  and the MPI modes: pgf help environment.

Options:
  -c COMMAND          run COMMAND with bash -c instead of an interactive shell
  -- COMMAND ...      run COMMAND and its arguments as they are
  --use SEL           the build: key or key prefix, commit, worktree, prefix path or auto (default: auto)
  --prefix DIR        use this complete prefix as it is; no staleness check
  --for-tree          same as --use auto
  --bare              no build: OpenFOAM only
  --yade              the DEM variant, plus yade, YADE_TRUNK and PYTHONPATH (default: PGF_YADE)
  --no-yade           the plain variant even when PGF_YADE=1
  --mpi MODE          Open MPI on lo only (loopback), untouched (default), or auto (default: PGF_MPI, auto)
  --set KEY=VALUE     override one setting for this call
  --foam-bashrc FILE  same as --set PGF_FOAM_BASHRC=FILE
  --yade-bin FILE     same as --set YADE_BIN=FILE
  --yade-trunk DIR    same as --set YADE_TRUNK=DIR

Examples:
  $ pgf shell
  $ pgf shell -- porousGasificationFoam -help
  $ pgf shell --yade -- yade -v

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help test

```text
pgf test: run a test tier against the checkout's build

Usage:
  pgf test <tier|all> [--rebuild] [--if-stale AGE] [--run-root DIR] | --status | --gc

Description:
  Builds (or reuses) the tier's build, runs its cases and records the outcome
  in ~/.local/state/pgf/runs/last/<tier>.json. Tiers are in
  applications/test/tiers.conf: fast, convergence, dem. Exit 0 PASS, 1 FAIL,
  2 ERROR, 124 TIMEOUT.

  dem first checks yade (YADE_BIN, or yade on PATH) and refuses, before the
  build, one not built at the yade-pgf pin or that does not say which commit
  built it: its baselines were made with the pin. PGF_YADE_OFF_PIN=1 runs it
  anyway with a WARN naming both commits.

Options:
  --rebuild          delete the tier's build first, then build it again
  --if-stale AGE     run only when the last PASS is older than AGE or of other sources
  --run-root DIR     the run dir (default: $PGF_RUN_ROOT/pgf-test-<tier>-<time>-<pid>)
  --status           per tier: last outcome, when, and whether it is current
  --gc               prune old run dirs and builds
  --keep-runs N      with --gc: run dirs to keep (default: 10)
  --keep-prefixes M  with --gc: builds to keep (default: 5)
  --dry-run          with --gc: only list what would go

Examples:
  $ pgf test fast
  $ pgf test fast --if-stale 7d
  $ pgf test --status
  $ PGF_YADE_OFF_PIN=1 pgf test dem  # with a yade off the pin, on purpose

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help status

```text
pgf status: what pgf uses and where: files, the build, your case copies

Usage:
  pgf status [PATH] [--json] [options]

Description:
  One screen: the checkout pgf runs from and its branch, the settings files, the
  build store, the run records, scratch and the case root, the build pgf would
  use now and what chose it, that checkout's two builds (non-DEM, DEM), the yade
  DEM runs would use against the yade-pgf pin (when one is set or on PATH), and your
  case copies (pgf copy) per branch. With PATH, asks that checkout instead. Every setting
  with its layer: pgf config. pgf with no command runs status. Old name: where.

Options:
  --json              every setting with its layer, and the builds, as JSON
  --use SEL           the build: key or key prefix, commit, worktree, prefix path or auto (default: auto)
  --prefix DIR        use this complete prefix as it is; no staleness check
  --for-tree          same as --use auto
  --bare              no build: OpenFOAM only
  --yade              the DEM variant, plus yade, YADE_TRUNK and PYTHONPATH (default: PGF_YADE)
  --no-yade           the plain variant even when PGF_YADE=1
  --mpi MODE          Open MPI on lo only (loopback), untouched (default), or auto (default: PGF_MPI, auto)
  --set KEY=VALUE     override one setting for this call
  --foam-bashrc FILE  same as --set PGF_FOAM_BASHRC=FILE
  --yade-bin FILE     same as --set YADE_BIN=FILE
  --yade-trunk DIR    same as --set YADE_TRUNK=DIR

Examples:
  $ pgf status
  $ pgf status ~/Code/pgf-feature  # another checkout

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help pack

```text
pgf pack: an Apptainer image of a commit

Usage:
  pgf pack [BUILD] [--no-yade] [--output FILE] [--dry-run]

Description:
  Builds a self-contained Apptainer image of one commit: the image compiles the
  pinned yade-pgf and PGF itself. BUILD (a key, commit or worktree) names the
  commit; the default is HEAD of the checkout pgf runs from, which must be clean.
  A yade1 image takes about 30-60 min (yade-pgf is compiled), yade0 a few
  minutes. In the image, apptainer run IMAGE COMMAND runs a pgf command
  (status, run, copy ...); anything else is a script of the case in the current
  directory: apptainer run IMAGE ./Allrun.

Options:
  --no-yade      the plain variant (no yade in the image)
  --output FILE  the .sif to write (default: ./pgf-<commit12>-yade<0|1>.sif)
  --dry-run      write the build context and the image definition; build nothing

Examples:
  $ pgf pack --dry-run

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help tools

```text
pgf tools: every tool at the checkout root: what, why, use instead

Usage:
  pgf tools

Description:
  For each executable at the checkout root, what it does, why it exists and
  what to use instead. The text is the what/why/instead header of each script.

  Old names (#274, #287): pgf-env is gone (pgf shell, pgf setup, ...); pgf where,
  pgf ls and pgf exec still work as pgf status, pgf builds and pgf shell --. The
  root pgf-test forwards to pgf test, with a note on stderr, for a deprecation
  period. Tests for pgf itself are applications/test/tooling/test_*.sh; each runs
  alone with PGF_FOAM_BASHRC set and builds under PGF_BUILD_ROOT (default
  $TMPDIR/pgf-tooling-builds).

Examples:
  $ pgf tools

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help help

```text
pgf help: help about a command or a topic

Usage:
  pgf help [command | topic | --all]

Description:
  Prints the help of pgf, of one command or of one topic; pgf <command> --help
  is the same as pgf help <command>. Topics explain what several commands share;
  pgf help lists them. --all prints every page in one go, through less on a
  terminal ($PAGER if set), plain when piped: pgf help --all | grep ...

Options:
  --all  every page: pgf, each command, each topic

Examples:
  $ pgf help setup
  $ pgf help environment  # a topic
  $ pgf help --all        # everything, paged

Global options: --color WHEN, -h/--help. All commands: pgf help.
```

## pgf help build-selection

```text
pgf help build-selection: which build a command uses, build keys, stale builds

Builds and keys:
  pgf build compiles a checkout into the build store (PGF_BUILD_ROOT, default
  $WM_PROJECT_USER_DIR/pgf-builds), one prefix per key. The key is made of the
  sources, the variant (yade0 non-DEM, yade1 DEM), the OpenFOAM version and the
  compiler, so a prefix with the same key is reused without compiling. Each
  checkout has two keys, non-DEM and DEM; pgf status shows them, pgf builds lists
  every build. More: doc/build.md, "Where things land".

Choosing a build:
  Each command uses one build. It comes from, later winning:
    auto              the build of the sources as they are now in the checkout
                      pgf runs from (the default)
    .pgf.conf         PGF_BUILD in the checkout file
    the terminal      PGF_BUILD in the environment, which pgf use sets
    --use SEL         on the command line
  PGF_BUILD is refused in the user file: it is per worktree or terminal.

  SEL is a key or unique key prefix, a commit (the newest build made from it), a
  worktree name or path (its last build), a prefix path, or auto. --prefix DIR
  uses that complete prefix as it is (also one built outside the store) and
  skips the staleness check. eval "$(pgf use SEL)" sets the terminal layer.
  The banner of pgf shell and pgf run names the layer the build came from.

Stale builds:
  A selected build that is not of that checkout's current sources is stale:
  pgf shell warns, pgf run refuses. pgf run --build builds that checkout first
  (it cannot be combined with a selected build); pgf run --allow-stale runs a
  stale build anyway. The refusal names the layer that chose the build and its
  fix: leave out --use; eval "$(pgf use auto)" for the terminal layer;
  pgf config unset PGF_BUILD --checkout for the checkout file.

  A case made by pgf copy runs with the build of the checkout it came from, unless
  a build was chosen.

All commands and topics: pgf help.
```

## pgf help run-record

```text
pgf help run-record: what pgf run writes about each run, and where

The record:
  After the script ends, pgf run writes <case>/.pgf-run.json, also after Ctrl-C:
    case, command            the case directory and the script with its arguments
    started_utc, finished_utc
    exit_status              the script's status, which pgf run also exits with
    build                    key, prefix, variant, git_head (the commit) and
                             source: the layer that selected it
    yade                     DEM runs only: the yade binary, its commit and the
                             yade-pgf pin; null otherwise

The log:
  The same record is appended as one line to
  ${XDG_STATE_HOME:-~/.local/state}/pgf/runs/runs.jsonl when that is writable
  (a read-only home, as in an Apptainer image, keeps only the case's record).
  Before #274 this log lived under the build root. pgf test keeps its tier
  results beside it, in last/<tier>.json.

Other files in a case:
  <case>/.pgf-source.json, written by pgf copy: the checkout and commit the copy
  came from.

All commands and topics: pgf help.
```

## pgf help environment

```text
pgf help environment: the clean environment of pgf shell, run and build

What is kept:
  pgf drops the caller's environment except HOME USER LANG TERM TMPDIR NO_COLOR
  WM_PROJECT_DIR XDG_CONFIG_HOME XDG_STATE_HOME and every PGF_* and YADE_*
  variable. Nested pgf calls inside it skip that step. pgf test keeps the
  caller's environment (it calls pgf shell itself), and pgf view runs your own
  ParaView.

Python:
  Every pgf command except view puts utilities/pgf/python/ first on PATH. Its
  python3 runs $PGF_PYTHON, an absolute path (default /usr/bin/python3, the
  distro's), so pgf, the cases it runs and a yade built with
  -DPYTHON_EXECUTABLE=/usr/bin/python3 share one Python, whatever else PATH holds
  (conda, a venv, pyenv, /usr/local/bin). pgf doctor shows the Python and checks
  that yade runs on it.

What is set:
  pgf sources $PGF_FOAM_BASHRC (default $WM_PROJECT_DIR/etc/bashrc) with set +eu.
  Then it points WM_PROJECT_USER_DIR, FOAM_USER_APPBIN and FOAM_USER_LIBBIN at
  the build and removes every PATH and LD_LIBRARY_PATH entry under the user dir
  the bashrc chose. It puts utilities/pgf/shims/ first on PATH. Its wmake
  refuses to install into the build while WM_PROJECT_USER_DIR, FOAM_USER_APPBIN
  and FOAM_USER_LIBBIN point at it (edit, then pgf build); a case's dynamicCode/
  passes. With --bare, wmake is OpenFOAM's own. It also sets UCX_VFS_ENABLE=n
  UCX_LOG_LEVEL=fatal, FOAM_RUN=$PGF_RUN_ROOT and TMPDIR=$PGF_RUN_ROOT/tmp
  (PGF_RUN_ROOT default: $TMPDIR/pgf-runs-$USER; pgf run of a DEM case gets its own
  TMPDIR below it, removed afterwards), and exports PGF_ROOT, so a copied
  case finds the checkout's shared data. It checks that porousGasificationFoam
  resolves into the build.

  On stderr it prints what it uses: OpenFOAM, the build and what chose it, and
  MPI when it restricts it, except on a serial pgf run (a non-DEM case without
  system/decomposeParDict), where the restriction still applies but is not
  printed. While it runs, pgf shell or pgf run holds a shared flock on
  $PGF_BUILD_ROOT/<key>.use.lock, which keeps pgf gc off the build.

  --bare uses no build: OpenFOAM only; pgf build runs the build engine in it.

Yade (--yade):
  --yade adds YADE_TRUNK (default submodules/yade-pgf), a yade on PATH (from
  YADE_BIN or the caller's PATH, linked as plain yade) and PYTHONPATH
  (YADE_PYTHONPATH, or found beside the yade install). It checks the coupling
  stamp and yade's version against the pin. If /tmp is read-only, it also
  preloads a small shim that moves yade's /tmp/yade-XXXXXX into $TMPDIR.

MPI (--mpi, PGF_MPI):
  loopback restricts Open MPI to self,vader,tcp on lo. A DEM run starts Yade and
  the solver in one mpirun (pgf dem run), so its ranks reach each other over shared
  memory; loopback keeps Open MPI's TCP off docker or libvirt bridges, which it
  can hang probing, and lets a host or sandbox whose only interface is lo run
  at all. The default, auto, applies loopback when a
  docker*, virbr*, br-* or veth* interface is up or when not exactly one other
  interface is up, and says so in the banner (MPI: local only (lo), because ...).
  default leaves Open MPI alone. Runs are single-host; auto does not handle
  multi-host jobs.

All commands and topics: pgf help.
```
