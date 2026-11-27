#!/bin/bash
# cli.sh — pgf's command table (#274, #285): one entry per subcommand drives
# pgf help, pgf <cmd> --help and pgf completion bash. Source-only; needs
# config.sh, ui.sh and PGF_ROOT.

PGF_CMDS=()
declare -gA PGF_CMD_SUM=() PGF_CMD_USE=() PGF_CMD_DESC=() PGF_CMD_OPTS=() PGF_CMD_ARGS=()
declare -gA PGF_CMD_OPTROWS=() PGF_CMD_EX=()

# pgf_cmd NAME SUMMARY USAGE OPTIONS ARGS — OPTIONS are the words to complete
#   (@env: the environment options); ARGS names the positional values.
pgf_cmd() {
  PGF_CMDS+=("$1")
  PGF_CMD_SUM[$1]="$2" PGF_CMD_USE[$1]="$3" PGF_CMD_OPTS[$1]="$4" PGF_CMD_ARGS[$1]="$5"
}
# pgf_cmd_desc NAME TEXT — the Description paragraph(s) of NAME's help page.
pgf_cmd_desc() { PGF_CMD_DESC[$1]="$2"; }
# pgf_cmd_opt NAME FLAGS MEANING [DEFAULT] — one Options row; FLAGS @env adds the
#   environment options.
pgf_cmd_opt() { PGF_CMD_OPTROWS[$1]+="$2"$'\t'"$3"$'\t'"${4:-}"$'\n'; }
# pgf_cmd_ex NAME COMMAND [WHAT] — one Examples row.
pgf_cmd_ex() { PGF_CMD_EX[$1]+="$2"$'\t'"${3:-}"$'\n'; }

PGF_ENV_OPTS="--use --prefix --for-tree --bare --yade --no-yade --mpi --set --foam-bashrc --yade-bin --yade-trunk"
# Options that take a value, and what the value is.
declare -gA PGF_OPT_VALUE=([--use]=build [--prefix]=dir [--mpi]=mpi [--set]=setting
  [--foam-bashrc]=file [--yade-bin]=file [--yade-trunk]=dir [--output]=file [--run-root]=dir
  [--keep]=none [--keep-runs]=none [--keep-prefixes]=none [--if-stale]=none [-c]=none
  [--png]=dir [--time]=none [--size]=none [--compare]=dir [--palette]=none
  [-ndem]=none [-nfoam]=none [-solver]=none [--stall-timeout]=none)

# The environment options of shell, run, status and doctor.
PGF_ENV_ROWS="--use SEL	the build: key or key prefix, commit, worktree, prefix path or auto	auto
--prefix DIR	use this complete prefix as it is; no staleness check
--for-tree	same as --use auto
--bare	no build: OpenFOAM only
--yade	the DEM variant, plus yade, YADE_TRUNK and PYTHONPATH	PGF_YADE
--no-yade	the plain variant even when PGF_YADE=1
--mpi MODE	Open MPI on lo only (loopback), untouched (default), or auto	PGF_MPI, auto
--set KEY=VALUE	override one setting for this call
--foam-bashrc FILE	same as --set PGF_FOAM_BASHRC=FILE
--yade-bin FILE	same as --set YADE_BIN=FILE
--yade-trunk DIR	same as --set YADE_TRUNK=DIR
"

# The pgf help groups, as doc/pgf.md has them; completion works but stays out
# of the list. Old names map to their command.
PGF_HELP_GROUPS=("Settings Commands" "Build Commands" "Run Commands" "Other Commands")
declare -gA PGF_HELP_GROUP=(["Settings Commands"]="setup config doctor" ["Build Commands"]="build builds use gc"
  ["Run Commands"]="copy run dem view shell test" ["Other Commands"]="status pack tools help")
declare -gA PGF_CMD_ALIAS=([where]=status [ls]=builds [exec]=shell)

pgf_cmd setup "find OpenFOAM and yade, save them, put pgf on PATH" \
  "pgf setup [--yes] [--user | --checkout] [--no-link | --link-only] [--force] [--no-completion]
            [--foam-bashrc FILE] [--yade-bin FILE] [--yade-trunk DIR] [--set KEY=VALUE]" \
  "--user --checkout --yes --no-link --link-only --force --no-completion --set --foam-bashrc --yade-bin --yade-trunk" none
pgf_cmd_desc setup "pgf runs every build and case in a clean environment, so it has to know where
OpenFOAM (and, for DEM cases, yade) lives. setup finds them, saves them in a
settings file that every pgf call reads, links ~/.local/bin/pgf to the
checkout pgf runs from and installs bash completion. It prints one line per step as it goes,
then what it saved where, then what to do next.

On a terminal it asks when there is a choice; --yes takes the detected choices
without asking. Rerun it any time: it keeps the values you set and says what
changed. Change a value later with pgf config set KEY=VALUE; what each setting
means and how the layers combine: pgf help config.

It looks for OpenFOAM at \$WM_PROJECT_DIR/etc/bashrc, ~/OpenFOAM/*/etc/bashrc,
/opt/openfoam* and /usr/lib/openfoam/* (v2606 first), and for yade or a
suffixed yade-* on your PATH. Values given with --foam-bashrc, --yade-bin,
--yade-trunk or --set win, and keys already in the file are kept. --checkout
also records submodules/yade-pgf as YADE_TRUNK when it is checked out.

The link step refuses to replace a link to another checkout without --force.
With several worktrees the link picks one; call the others by path.
--link-only does only the link: no settings, no completion."
pgf_cmd_opt setup "-y, --yes" "do not ask: take what was detected, install completion"
pgf_cmd_opt setup --user "save to ~/.config/pgf/env.conf, for every checkout" "yes"
pgf_cmd_opt setup --checkout "save to <checkout>/.pgf.conf, for that checkout only"
pgf_cmd_opt setup --no-link "do not link ~/.local/bin/pgf"
pgf_cmd_opt setup --link-only "only link ~/.local/bin/pgf to the checkout pgf runs from"
pgf_cmd_opt setup --force "replace a link that points at another checkout"
pgf_cmd_opt setup --no-completion "do not install bash completion"
pgf_cmd_opt setup "--foam-bashrc FILE" "this OpenFOAM etc/bashrc instead of a detected one"
pgf_cmd_opt setup "--yade-bin FILE" "this yade binary instead of a detected one"
pgf_cmd_opt setup "--yade-trunk DIR" "this yade-pgf source tree"
pgf_cmd_opt setup "--set KEY=VALUE" "save any setting (keys: pgf help config)"
pgf_cmd_ex setup "./pgf setup" "ask, then save and link"
pgf_cmd_ex setup "./pgf setup --yes" "take the detected choices"
pgf_cmd_ex setup "./pgf setup --foam-bashrc ~/OpenFOAM/OpenFOAM-v2606/etc/bashrc"
pgf_cmd_ex setup "./pgf setup --link-only --force" "make pgf on PATH the ./pgf you call"

pgf_cmd build "build the checkout (libraries and solver)" "pgf build [--yade] [-v] [build.sh options]" \
  "--yade --set --foam-bashrc" build-options
pgf_cmd_desc build "Builds the checkout pgf runs from into the build store, in a prefix named by
a key made of the sources, the variant, the OpenFOAM version and the compiler. A prefix with
the same key is reused without compiling. wmake's output goes to
<prefix>/build.log; the screen shows one line per target. ./Allwmake runs this.
Switching between the normal and DEM variants needs no clean: pgf build drops the
other variant's objects of the targets that differ and says so. After editing the
sources, run pgf build again; wmake inside pgf shell is refused."
pgf_cmd_opt build --yade "the DEM variant (WITH_YADE=1)"
pgf_cmd_opt build "-v, --verbose" "stream wmake's output instead of logging it"
pgf_cmd_opt build --libs-only "only the libraries"
pgf_cmd_opt build --dry-run "show what would be built; build nothing"
pgf_cmd_opt build --print-prefix "print the prefix the checkout builds into"
pgf_cmd_opt build clean "clean the in-tree cache (lnInclude/, Make/<WM_OPTIONS>/); prefixes stay"
pgf_cmd_ex build "pgf build"
pgf_cmd_ex build "pgf build --yade" "the DEM variant"
pgf_cmd_ex build "utilities/pgf/build.sh --help" "every build.sh option"

pgf_cmd copy "copy a tutorial case out of the checkout, ready to run" "pgf copy <tutorial> [NAME] [--force] | pgf copy --list" \
  "--list --force" tutorial
pgf_cmd_desc copy "Copies tutorials/cases/<tutorial>, the whole case with linked files resolved
(cp -rL), to \$PGF_CASE_ROOT/<branch>/<tutorial>, or <branch>/NAME. <branch> is
the branch of the checkout pgf runs from, with / as -, or its short commit.
PGF_CASE_ROOT defaults to OpenFOAM's run dir, \$WM_PROJECT_USER_DIR/run
(~/OpenFOAM/<user>-v2606/run), not pgf's scratch \$FOAM_RUN.

The copy records the checkout and commit it came from: pgf run then uses that
checkout's build, and warns when the branch has moved on since. The record is
<copy>/.pgf-source.json. An existing copy is kept unless --force. pgf status
lists your copies per branch."
pgf_cmd_opt copy --list "list the tutorials, one line each"
pgf_cmd_opt copy --force "replace an existing copy"
pgf_cmd_ex copy "pgf copy --list"
pgf_cmd_ex copy "pgf copy charOnlyMoveCases/serial" "prints the copy's path and the pgf run line"
pgf_cmd_ex copy "pgf copy canonical/pyrolysis pyrolysis-2" "a second copy under another name"

pgf_cmd run "run a case script and record the run" \
  "pgf run [options] [--build] [--allow-stale] <caseDir> [Allrun | Allclean | script] [args ...]" \
  "@env --build --allow-stale" case
pgf_cmd_desc run "Changes into caseDir and runs the script (default Allrun) in the clean
environment, then writes <caseDir>/.pgf-run.json and says how it went. It works
from any directory; caseDir is resolved from the current one. A case whose
constant/yadeProperties says active true gets --yade, unless --no-yade is given
or PGF_YADE is set in a file or the environment. Run a copy of a tutorial
(pgf copy), not the tutorial in the checkout: a copy runs with the build of the
checkout it came from. A stale build is refused; the refusal names the layer
that chose it and the fix. run exits with the script's status.

The run record: pgf help run-record. The environment: pgf help environment.
Choosing a build and staleness: pgf help build-selection."
pgf_cmd_opt run --build "build the checkout pgf runs from first"
pgf_cmd_opt run --allow-stale "run with a build of other sources"
pgf_cmd_opt run @env
pgf_cmd_ex run "pgf copy solidAdvection" "prints where the copy went"
pgf_cmd_ex run "pgf run ~/OpenFOAM/\$USER-v2606/run/main/solidAdvection" "the copy of a checkout on main"
pgf_cmd_ex run "pgf run ~/OpenFOAM/\$USER-v2606/run/main/solidAdvection Allclean"

pgf_cmd dem "coupled Yade + solver runs (DEM cases)" \
  "pgf dem run [-ndem N] [-nfoam M] [-solver NAME] [--stall-timeout S] SCRIPT.py [yade args ...]" \
  "-ndem -nfoam -solver --stall-timeout" dem
pgf_cmd_desc dem "pgf dem run starts a coupled DEM run from the case directory: Yade and the
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
tutorials/cases/DEM_common/pgfDemRun is its old name."
pgf_cmd_opt dem "-ndem N" "Yade ranks, mpy's master included" 2
pgf_cmd_opt dem "-nfoam M" "OpenFOAM ranks" "numberOfSubdomains"
pgf_cmd_opt dem "-solver NAME" "the solver" porousGasificationFoam
pgf_cmd_opt dem "--stall-timeout S" "stop a run silent for S seconds; 0: never" "PGF_DEM_STALL_TIMEOUT, 600"
pgf_cmd_ex dem "pgf dem run -ndem 2 MPI_lambda.py" "in an Allrun, from the case directory"

pgf_cmd view "look at a case's results in ParaView (pgfView)" \
  "pgf view <caseDir> [--png DIR [--time T] [--size WxH] [--xvfb]] [--compare OTHER] [--palette NAME]" \
  "--png --time --size --compare --palette --xvfb" dir
pgf_cmd_desc view "Opens the case in ParaView with the views its system/paraviewViews.py asks for,
one layout tab each, or with --png renders them to PNGs without a screen. The
GUI keeps your ParaView palette; --png uses WhiteBackground. ParaView comes
from your PATH, not pgf's clean environment. This is
applications/utilities/paraview/pgfView; more in doc/paraview.md."
pgf_cmd_opt view "--png DIR" "write DIR/<view>.png instead of opening the GUI"
pgf_cmd_opt view "--time T" "with --png: this time" "the last mesh time"
pgf_cmd_opt view "--size WxH" "with --png: the image size" 1000x750
pgf_cmd_opt view --xvfb "with --png: render under xvfb-run (also when DISPLAY is unset)"
pgf_cmd_opt view "--compare OTHER" "the case, OTHER and their difference, side by side"
pgf_cmd_opt view "--palette NAME" "a ParaView palette, e.g. BlackBackground"
pgf_cmd_ex view "pgf view ~/OpenFOAM/\$USER-v2606/run/main/solidAdvection"
pgf_cmd_ex view "pgf view <caseDir> --png out/" "headless: out/<view>.png"

pgf_cmd test "run a test tier against the checkout's build" \
  "pgf test <tier|all> [--rebuild] [--if-stale AGE] [--run-root DIR] | --status | --gc" \
  "--rebuild --if-stale --run-root --status --gc --keep-runs --keep-prefixes --dry-run" tier
pgf_cmd_desc test "Builds (or reuses) the tier's build, runs its cases and records the outcome
in ~/.local/state/pgf/runs/last/<tier>.json. Tiers are in
applications/test/tiers.conf: fast, convergence, dem. Exit 0 PASS, 1 FAIL,
2 ERROR, 124 TIMEOUT.

dem first checks yade (YADE_BIN, or yade on PATH) and refuses, before the
build, one not built at the yade-pgf pin or that does not say which commit
built it: its baselines were made with the pin. PGF_YADE_OFF_PIN=1 runs it
anyway with a WARN naming both commits."
pgf_cmd_opt test --rebuild "delete the tier's build first, then build it again"
pgf_cmd_opt test "--if-stale AGE" "run only when the last PASS is older than AGE or of other sources"
pgf_cmd_opt test "--run-root DIR" "the run dir" '$PGF_RUN_ROOT/pgf-test-<tier>-<time>-<pid>'
pgf_cmd_opt test --status "per tier: last outcome, when, and whether it is current"
pgf_cmd_opt test --gc "prune old run dirs and builds"
pgf_cmd_opt test "--keep-runs N" "with --gc: run dirs to keep" 10
pgf_cmd_opt test "--keep-prefixes M" "with --gc: builds to keep" 5
pgf_cmd_opt test --dry-run "with --gc: only list what would go"
pgf_cmd_ex test "pgf test fast"
pgf_cmd_ex test "pgf test fast --if-stale 7d"
pgf_cmd_ex test "pgf test --status"
pgf_cmd_ex test "PGF_YADE_OFF_PIN=1 pgf test dem" "with a yade off the pin, on purpose"

pgf_cmd status "what pgf uses and where: files, the build, your case copies" "pgf status [PATH] [--json] [options]" "--json @env" dir
pgf_cmd_desc status "One screen: the checkout pgf runs from and its branch, the settings files, the
build store, the run records, scratch and the case root, the build pgf would
use now and what chose it, that checkout's two builds (non-DEM, DEM), the yade
DEM runs would use against the yade-pgf pin (when one is set or on PATH), and your
case copies (pgf copy) per branch. With PATH, asks that checkout instead. Every setting
with its layer: pgf config. pgf with no command runs status. Old name: where."
pgf_cmd_opt status --json "every setting with its layer, and the builds, as JSON"
pgf_cmd_opt status @env
pgf_cmd_ex status "pgf status"
pgf_cmd_ex status "pgf status ~/Code/pgf-feature" "another checkout"

pgf_cmd doctor "check that pgf can build and run, with a fix for each problem" "pgf doctor [options]" "@env" none
pgf_cmd_desc doctor "Read-only checks in groups: OpenFOAM, the builds of the checkout pgf runs
from, yade and DEM, MPI, ParaView, and pgf on PATH. Each check is one OK, WARN or FAIL line; under a WARN
or FAIL, the detail and the command that fixes it. It ends with a one-line
verdict and exits 1 when a check fails. The settings themselves: pgf config.

It checks the OpenFOAM bashrc and version, the build of the checkout pgf runs
from, yade (the binary, its version against the yade-pgf pin, MPI among its
features), the coupling stamp, the MPI decision and the interfaces it saw, whether
the ParaView on your PATH has Python for pgf view, and whether pgf is on PATH."
pgf_cmd_opt doctor @env
pgf_cmd_ex doctor "pgf doctor"
pgf_cmd_ex doctor "pgf doctor --yade" "also check the DEM build"

pgf_cmd config "show every setting with its layer, or change one" \
  "pgf config [get KEY | set KEY=VALUE | unset KEY] [--user | --checkout] [--json]" "--user --checkout --json" config
pgf_cmd_desc config "Each setting comes from, later winning: the default, ~/.config/pgf/env.conf
(user layer), <checkout>/.pgf.conf (checkout layer), the environment, the
command line. Without a verb, config prints every setting and its layer
(file:line for a file, [default] for a built-in default); set and unset edit
the user file, or the checkout file with --checkout. get prints one resolved
value and exits 1 when the key is unset.

The user file is \${XDG_CONFIG_HOME:-~/.config}/pgf/env.conf. The checkout file
is .pgf.conf at the checkout root; the old name .pgf-env.conf is still read,
with a note, while no .pgf.conf exists, and the next set --checkout or
pgf setup --checkout writes .pgf.conf. The files hold KEY=value lines and #
comments. They are parsed, not sourced: an unknown key or a malformed line
exits 2 naming file:line, so a file cannot set PATH or run code. PGF_BUILD is
refused in the user file (pgf help build-selection).

On the command line, --set KEY=VALUE overrides one key, and --foam-bashrc,
--yade / --no-yade and --mpi set theirs. PGF_YADE=1 makes --yade the default;
PGF_MPI=loopback does what --mpi loopback does (pgf help environment)."
pgf_cmd_opt config --user "edit ~/.config/pgf/env.conf" "yes"
pgf_cmd_opt config --checkout "edit <checkout>/.pgf.conf"
pgf_cmd_opt config --json "print the settings and layers as JSON"
pgf_cmd_ex config "pgf config"
pgf_cmd_ex config "pgf config set PGF_FOAM_BASHRC=~/OpenFOAM/OpenFOAM-v2606/etc/bashrc"
pgf_cmd_ex config "pgf config set PGF_MPI=loopback --checkout" "only the checkout pgf runs from"
pgf_cmd_ex config "pgf config get YADE_BIN"

pgf_cmd builds "every build, and the checkouts that made them" "pgf builds [--json]" "--json" none
pgf_cmd_desc builds "Lists the checkouts that built into the build store and still exist (and how
many no longer do; pgf gc forgets them), then every build: key, variant, age,
size, the checkout that built it, the existing checkouts whose last build it is
(last build of ...), [in use] and [building]. A legend and the total count and
size close the list: * marks the builds of the current sources of the checkout
pgf runs from, and (incomplete) a build that stopped before it finished, which
gc removes. Old name: ls. What a build and its key are: pgf help build-selection."
pgf_cmd_opt builds --json "as JSON"
pgf_cmd_ex builds "pgf builds"

pgf_cmd use "choose a build for this terminal (with eval)" "eval \"\$(pgf use <key|commit|worktree|prefix|auto> [--yade])\"" \
  "--yade --no-yade" build
pgf_cmd_desc use "Prints the shell line that makes a build this terminal's: wrap it in eval.
Without eval it only prints that line and changes nothing. Inside pgf shell,
plain pgf use does the eval. auto goes back to the build of the sources of the
checkout pgf runs from.

Run without eval on a terminal, it says it only printed the line; under eval
it says the terminal now uses the build. Either way it notes when the build is
not of the checkout's current sources. Inside pgf shell, the shell itself keeps
the build it started with; later pgf shell and pgf run calls follow the new
choice. What SEL can be: pgf help build-selection."
pgf_cmd_opt use --yade "pick from the DEM builds"
pgf_cmd_opt use --no-yade "pick from the plain builds"
pgf_cmd_ex use 'eval "$(pgf use a2055)"' "a key prefix"
pgf_cmd_ex use 'eval "$(pgf use auto)"'

pgf_cmd gc "remove old unused builds" "pgf gc [--keep N] [--yes] [--dry-run] [--json]" "--keep --yes --dry-run --json" none
pgf_cmd_desc gc "Removes builds that no existing checkout last built, that are not in use or
being built, beyond the newest N, and forgets checkouts that no longer exist.
The current builds of the checkout pgf runs from always stay. Each kept build
gives its reason (a build of <name>'s current sources, chosen with pgf use or
PGF_BUILD, in use, building, last build of <checkout>, one of the N newest
others, --keep), and each removed one says if it is incomplete.

It ends with a summary: N build(s) to remove, SIZE; M kept, plus the checkouts
that no longer exist, whose records a real gc forgets. --dry-run ends with
nothing removed (--dry-run). Without --yes it asks on a terminal, and refuses
(exit 2) without one. In use means a pgf shell or pgf run holds the build's
lock (pgf help environment); pgf test --gc skips those builds too."
pgf_cmd_opt gc "--keep N" "keep the N newest" 2
pgf_cmd_opt gc --yes "remove without asking"
pgf_cmd_opt gc --dry-run "only list what would go"
pgf_cmd_opt gc --json "as JSON"
pgf_cmd_ex gc "pgf gc --dry-run"

pgf_cmd shell "a shell, or one command, in pgf's clean environment" "pgf shell [options] [-c COMMAND | -- COMMAND ...]" "@env -c --" command
pgf_cmd_desc shell "Starts bash with the caller's environment dropped (a small allowlist kept),
OpenFOAM's bashrc sourced and the user dirs pointed at the build, with the
prompt (pgf:<worktree>@<key>); pgf calls inside it keep that environment.
With -- COMMAND it runs that command instead (old name: pgf exec). A build
not made from the sources of the checkout pgf runs from is stale: shell warns.
--for-tree exits 2 when that checkout is not built. What the environment holds
and the MPI modes: pgf help environment."
pgf_cmd_opt shell "-c COMMAND" "run COMMAND with bash -c instead of an interactive shell"
pgf_cmd_opt shell "-- COMMAND ..." "run COMMAND and its arguments as they are"
pgf_cmd_opt shell @env
pgf_cmd_ex shell "pgf shell"
pgf_cmd_ex shell "pgf shell -- porousGasificationFoam -help"
pgf_cmd_ex shell "pgf shell --yade -- yade -v"

pgf_cmd pack "an Apptainer image of a commit" "pgf pack [BUILD] [--no-yade] [--output FILE] [--dry-run]" \
  "--yade --no-yade --output --dry-run" build
pgf_cmd_desc pack "Builds a self-contained Apptainer image of one commit: the image compiles the
pinned yade-pgf and PGF itself. BUILD (a key, commit or worktree) names the
commit; the default is HEAD of the checkout pgf runs from, which must be clean.
A yade1 image takes about 30-60 min (yade-pgf is compiled), yade0 a few
minutes. In the image, apptainer run IMAGE COMMAND runs a pgf command
(status, run, copy ...); anything else is a script of the case in the current
directory: apptainer run IMAGE ./Allrun."
pgf_cmd_opt pack --no-yade "the plain variant (no yade in the image)"
pgf_cmd_opt pack "--output FILE" "the .sif to write" "./pgf-<commit12>-yade<0|1>.sif"
pgf_cmd_opt pack --dry-run "write the build context and the image definition; build nothing"
pgf_cmd_ex pack "pgf pack --dry-run"

pgf_cmd tools "every tool at the checkout root: what, why, use instead" "pgf tools" "" none
pgf_cmd_desc tools "For each executable at the checkout root, what it does, why it exists and
what to use instead. The text is the what/why/instead header of each script.

Old names (#274, #287): pgf-env is gone (pgf shell, pgf setup, ...); pgf where,
pgf ls and pgf exec still work as pgf status, pgf builds and pgf shell --. The
root pgf-test forwards to pgf test, with a note on stderr, for a deprecation
period. Tests for pgf itself are applications/test/tooling/test_*.sh; each runs
alone with PGF_FOAM_BASHRC set and builds under PGF_BUILD_ROOT (default
\$TMPDIR/pgf-tooling-builds)."
pgf_cmd_ex tools "pgf tools"

pgf_cmd completion "print the bash completion script" "pgf completion bash" "" shell
pgf_cmd_desc completion "Prints the bash completion script. pgf setup installs it to
\${BASH_COMPLETION_USER_DIR:-\${XDG_DATA_HOME:-~/.local/share}/bash-completion}/completions/pgf,
where the bash-completion package loads it on the first Tab; it asks on a
terminal, installs with --yes and skips with --no-completion. Without that
package, source the file from ~/.bashrc. Rerun pgf setup (or this command)
after updating pgf.

The script is made from the same command table as pgf help, so the two cannot
drift. It completes every command, its options, help topics, and these values,
asking pgf __complete at Tab time: build keys, auto and worktree names after
use, pack and --use; get/set/unset, then keys after config (and KEY= then
values for config set and --set); auto/loopback/default for --mpi and PGF_MPI=,
0/1 for PGF_YADE=; directories after run, then that case's scripts; tiers from
applications/test/tiers.conf (and all) after test; build.sh options after build.

Build keys need the build root. When PGF_BUILD_ROOT is not set, finding it
sources OpenFOAM, which takes about a second; set PGF_BUILD_ROOT to make that
Tab instant."
pgf_cmd_ex completion "pgf completion bash > ~/.local/share/bash-completion/completions/pgf"

pgf_cmd help "help about a command or a topic" "pgf help [command | topic | --all]" "--all" cmd
pgf_cmd_desc help "Prints the help of pgf, of one command or of one topic; pgf <command> --help
is the same as pgf help <command>. Topics explain what several commands share;
pgf help lists them. --all prints every page in one go, through less on a
terminal (\$PAGER if set), plain when piped: pgf help --all | grep ..."
pgf_cmd_opt help --all "every page: pgf, each command, each topic"
pgf_cmd_ex help "pgf help setup"
pgf_cmd_ex help "pgf help environment" "a topic"
pgf_cmd_ex help "pgf help --all" "everything, paged"

# Help topics (pgf help <topic>), like git help <topic>: what several commands
# share. "## Title" lines are headings; the rest is indented as it is.
PGF_TOPICS=()
declare -gA PGF_TOPIC_SUM=() PGF_TOPIC_TEXT=()
pgf_topic() {
  PGF_TOPICS+=("$1")
  PGF_TOPIC_SUM[$1]="$2" PGF_TOPIC_TEXT[$1]="$3"
}

pgf_topic build-selection "which build a command uses, build keys, stale builds" "## Builds and keys
pgf build compiles a checkout into the build store (PGF_BUILD_ROOT, default
\$WM_PROJECT_USER_DIR/pgf-builds), one prefix per key. The key is made of the
sources, the variant (yade0 non-DEM, yade1 DEM), the OpenFOAM version and the
compiler, so a prefix with the same key is reused without compiling. Each
checkout has two keys, non-DEM and DEM; pgf status shows them, pgf builds lists
every build. More: doc/build.md, \"Where things land\".

## Choosing a build
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
skips the staleness check. eval \"\$(pgf use SEL)\" sets the terminal layer.
The banner of pgf shell and pgf run names the layer the build came from.

## Stale builds
A selected build that is not of that checkout's current sources is stale:
pgf shell warns, pgf run refuses. pgf run --build builds that checkout first
(it cannot be combined with a selected build); pgf run --allow-stale runs a
stale build anyway. The refusal names the layer that chose the build and its
fix: leave out --use; eval \"\$(pgf use auto)\" for the terminal layer;
pgf config unset PGF_BUILD --checkout for the checkout file.

A case made by pgf copy runs with the build of the checkout it came from, unless
a build was chosen."

pgf_topic run-record "what pgf run writes about each run, and where" "## The record
After the script ends, pgf run writes <case>/.pgf-run.json, also after Ctrl-C:
  case, command            the case directory and the script with its arguments
  started_utc, finished_utc
  exit_status              the script's status, which pgf run also exits with
  build                    key, prefix, variant, git_head (the commit) and
                           source: the layer that selected it
  yade                     DEM runs only: the yade binary, its commit and the
                           yade-pgf pin; null otherwise

## The log
The same record is appended as one line to
\${XDG_STATE_HOME:-~/.local/state}/pgf/runs/runs.jsonl when that is writable
(a read-only home, as in an Apptainer image, keeps only the case's record).
Before #274 this log lived under the build root. pgf test keeps its tier
results beside it, in last/<tier>.json.

## Other files in a case
<case>/.pgf-source.json, written by pgf copy: the checkout and commit the copy
came from."

pgf_topic environment "the clean environment of pgf shell, run and build" "## What is kept
pgf drops the caller's environment except HOME USER LANG TERM TMPDIR NO_COLOR
WM_PROJECT_DIR XDG_CONFIG_HOME XDG_STATE_HOME and every PGF_* and YADE_*
variable. Nested pgf calls inside it skip that step. pgf test keeps the
caller's environment (it calls pgf shell itself), and pgf view runs your own
ParaView.

## Python
Every pgf command except view puts utilities/pgf/python/ first on PATH. Its
python3 runs \$PGF_PYTHON, an absolute path (default /usr/bin/python3, the
distro's), so pgf, the cases it runs and a yade built with
-DPYTHON_EXECUTABLE=/usr/bin/python3 share one Python, whatever else PATH holds
(conda, a venv, pyenv, /usr/local/bin). pgf doctor shows the Python and checks
that yade runs on it.

## What is set
pgf sources \$PGF_FOAM_BASHRC (default \$WM_PROJECT_DIR/etc/bashrc) with set +eu.
Then it points WM_PROJECT_USER_DIR, FOAM_USER_APPBIN and FOAM_USER_LIBBIN at
the build and removes every PATH and LD_LIBRARY_PATH entry under the user dir
the bashrc chose. It puts utilities/pgf/shims/ first on PATH. Its wmake
refuses to install into the build while WM_PROJECT_USER_DIR, FOAM_USER_APPBIN
and FOAM_USER_LIBBIN point at it (edit, then pgf build); a case's dynamicCode/
passes. With --bare, wmake is OpenFOAM's own. It also sets UCX_VFS_ENABLE=n
UCX_LOG_LEVEL=fatal, FOAM_RUN=\$PGF_RUN_ROOT and TMPDIR=\$PGF_RUN_ROOT/tmp
(PGF_RUN_ROOT default: \$TMPDIR/pgf-runs-\$USER; pgf run of a DEM case gets its own
TMPDIR below it, removed afterwards), and exports PGF_ROOT, so a copied
case finds the checkout's shared data. It checks that porousGasificationFoam
resolves into the build.

On stderr it prints what it uses: OpenFOAM, the build and what chose it, and
MPI when it restricts it, except on a serial pgf run (a non-DEM case without
system/decomposeParDict), where the restriction still applies but is not
printed. While it runs, pgf shell or pgf run holds a shared flock on
\$PGF_BUILD_ROOT/<key>.use.lock, which keeps pgf gc off the build.

--bare uses no build: OpenFOAM only; pgf build runs the build engine in it.

## Yade (--yade)
--yade adds YADE_TRUNK (default submodules/yade-pgf), a yade on PATH (from
YADE_BIN or the caller's PATH, linked as plain yade) and PYTHONPATH
(YADE_PYTHONPATH, or found beside the yade install). It checks the coupling
stamp and yade's version against the pin. If /tmp is read-only, it also
preloads a small shim that moves yade's /tmp/yade-XXXXXX into \$TMPDIR.

## MPI (--mpi, PGF_MPI)
loopback restricts Open MPI to self,vader,tcp on lo. A DEM run starts Yade and
the solver in one mpirun (pgf dem run), so its ranks reach each other over shared
memory; loopback keeps Open MPI's TCP off docker or libvirt bridges, which it
can hang probing, and lets a host or sandbox whose only interface is lo run
at all. The default, auto, applies loopback when a
docker*, virbr*, br-* or veth* interface is up or when not exactly one other
interface is up, and says so in the banner (MPI: local only (lo), because ...).
default leaves Open MPI alone. Runs are single-host; auto does not handle
multi-host jobs."

# _pgf_help_rows ROWS — tab-separated rows as aligned columns; a first column
#   over 36 characters is not padded. A third column is the default ("yes": default).
_pgf_help_rows() {
  local rows="$1" w=0 a b c d
  while IFS=$'\t' read -r a b c; do [ ${#a} -gt "$w" ] && [ ${#a} -le 36 ] && w=${#a}; done <<<"${rows%$'\n'}"
  while IFS=$'\t' read -r a b c; do
    [ -n "$a" ] || continue
    d=""
    [ -z "$c" ] || d=" $(pgf_ui_paint 2 "$([ "$c" = yes ] && echo "(default)" || echo "(default: $c)")")"
    printf "  %-${w}s  %s%s\n" "$a" "$b" "$d"
  done <<<"$rows"
}

# _pgf_help_section TITLE — a bold heading, after a blank line.
_pgf_help_section() {
  printf '\n%s\n' "$(pgf_ui_paint 1 "$1:")"
}

# _pgf_help_examples ROWS — "$ command" lines, comments aligned after them.
_pgf_help_examples() {
  local rows="" a b
  while IFS=$'\t' read -r a b; do
    [ -n "$a" ] && rows+="\$ $a"$'\t'"${b:+# $b}"$'\n'
  done <<<"$1"
  _pgf_help_rows "$rows" | sed 's/ *$//'
}

# pgf_cli_help — the global page: summary, Usage, Description, commands,
#   global options, examples.
pgf_cli_help() {
  local c rows=""
  echo "pgf: settings, builds, runs and tests for porousGasificationFoam"
  _pgf_help_section Usage
  echo "  pgf <command> [options]"
  _pgf_help_section Description
  cat <<'EOF'
  pgf is the one entry point to porousGasificationFoam. It remembers where
  OpenFOAM and yade live, builds each checkout into its own prefix, and runs
  cases and test tiers in a clean environment with that build, so nothing
  depends on what your shell has sourced.
  Settings come from, later winning: the default, ~/.config/pgf/env.conf,
  <checkout>/.pgf.conf, the environment, the command line (pgf help config).
  If you only need the OpenFOAM-style entry point, ./Allwmake and ./Allwclean
  build and clean through pgf.
EOF
  local g w=0
  for c in "${PGF_CMDS[@]}" "${PGF_TOPICS[@]}"; do [ ${#c} -gt "$w" ] && w=${#c}; done
  for g in "${PGF_HELP_GROUPS[@]}"; do
    _pgf_help_section "$g"
    for c in ${PGF_HELP_GROUP[$g]}; do printf "  %-${w}s  %s\n" "$c" "${PGF_CMD_SUM[$c]}"; done
  done
  echo
  echo "  Old names still work: where (status), ls (builds), exec (shell --)."
  _pgf_help_section "Help Topics"
  for c in "${PGF_TOPICS[@]}"; do printf "  %-${w}s  %s\n" "$c" "${PGF_TOPIC_SUM[$c]}"; done
  _pgf_help_section Options
  _pgf_help_rows "--color WHEN	colour: auto (when output goes to a terminal and NO_COLOR is unset), always, never	auto
-h, --help	this help, or a command's"
  _pgf_help_section Examples
  _pgf_help_examples "./pgf setup	find OpenFOAM, save it, put pgf on PATH
pgf build	build the checkout pgf runs from
pgf copy solidAdvection	copy a tutorial out of the checkout; prints its path
pgf run ~/OpenFOAM/\$USER-v2606/run/main/solidAdvection	run that copy
pgf view ~/OpenFOAM/\$USER-v2606/run/main/solidAdvection	look at the results
pgf doctor	check the setup, with fixes"
  echo
  echo "Use \"pgf help <command>\" or \"pgf <command> --help\" for more about a command,"
  echo "\"pgf help <topic>\" for a topic, \"pgf help --all\" for every page."
}

# pgf_cli_help_topic TOPIC — one topic's page; returns 2 for an unknown topic.
pgf_cli_help_topic() {
  local t="$1" line prev=x
  [ -n "${PGF_TOPIC_SUM[$t]+x}" ] || return 2
  echo "pgf help $t: ${PGF_TOPIC_SUM[$t]}"
  # A blank line before a heading is the heading's own.
  while IFS= read -r line; do
    case "$line" in
    "## "*) _pgf_help_section "${line#\#\# }" ;;
    "") ;;
    *) [ -z "$prev" ] && echo; echo "  $line" ;;
    esac
    prev="$line"
  done <<<"${PGF_TOPIC_TEXT[$t]}"
  echo
  echo "All commands and topics: pgf help."
}

# pgf_cli_help_all — every page: pgf, each command in group order, each topic.
pgf_cli_help_all() {
  local c g rule
  rule="$(printf '%*s' 78 '' | tr ' ' '=')"
  pgf_cli_help
  for g in "${PGF_HELP_GROUPS[@]}"; do
    for c in ${PGF_HELP_GROUP[$g]} $([ "$g" = "Other Commands" ] && echo completion); do
      printf '\n%s\n\n' "$rule"
      pgf_cli_help_cmd "$c"
    done
  done
  for c in "${PGF_TOPICS[@]}"; do
    printf '\n%s\n\n' "$rule"
    pgf_cli_help_topic "$c"
  done
}

# pgf_cli_help_cmd CMD — one command's page: Usage, Description, Options, Examples.
pgf_cli_help_cmd() {
  local c="$1" rows="" line
  if [ -n "${PGF_CMD_ALIAS[$c]+x}" ]; then
    echo "pgf $c is an old name for pgf ${PGF_CMD_ALIAS[$c]}; it still works."
    echo
    c="${PGF_CMD_ALIAS[$c]}"
  fi
  [ -n "${PGF_CMD_SUM[$c]+x}" ] || return 2
  echo "pgf $c: ${PGF_CMD_SUM[$c]}"
  _pgf_help_section Usage
  sed 's/^/  /' <<<"${PGF_CMD_USE[$c]}"
  _pgf_help_section Description
  sed 's/^./  &/' <<<"${PGF_CMD_DESC[$c]}"
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    if [ "${line%%$'\t'*}" = @env ]; then rows+="$PGF_ENV_ROWS"; else rows+="$line"$'\n'; fi
  done <<<"${PGF_CMD_OPTROWS[$c]:-}"
  if [ -n "$rows" ]; then
    _pgf_help_section Options
    _pgf_help_rows "$rows"
  fi
  if [ "$c" = config ]; then
    _pgf_help_section Settings
    rows=""
    for line in "${PGF_CONF_KEYS[@]}"; do rows+="$line"$'\t'"${PGF_CONF_DESC[$line]:-}"$'\n'; done
    _pgf_help_rows "$rows"
  fi
  if [ -n "${PGF_CMD_EX[$c]:-}" ]; then
    _pgf_help_section Examples
    _pgf_help_examples "${PGF_CMD_EX[$c]}"
  fi
  echo
  echo "Global options: --color WHEN, -h/--help. All commands: pgf help."
}

# pgf_cli_opts CMD — the completion words of CMD's options.
pgf_cli_opts() {
  local o="${PGF_CMD_OPTS[$1]:-}"
  printf '%s' "${o/@env/$PGF_ENV_OPTS} --help"
}

# pgf_completion_cmd SHELL — print the completion script for SHELL (bash).
pgf_completion_cmd() {
  local c
  [ "${1:-}" = bash ] && [ $# -eq 1 ] || { echo "usage: pgf completion bash" >&2; return 2; }
  echo "# bash completion for pgf, generated by: pgf completion bash (#274)"
  echo "# Dynamic values come from: pgf __complete builds|tiers|tutorials|build-options"
  printf '_pgf_cmds=%q\n' "$(for c in "${PGF_HELP_GROUPS[@]}"; do printf '%s ' ${PGF_HELP_GROUP[$c]}; done)"
  printf 'declare -gA _pgf_opts=( '
  for c in "${PGF_CMDS[@]}"; do printf '[%s]=%q ' "$c" "$(pgf_cli_opts "$c")"; done
  for c in "${!PGF_CMD_ALIAS[@]}"; do printf '[%s]=%q ' "$c" "$(pgf_cli_opts "${PGF_CMD_ALIAS[$c]}")"; done
  printf ')\ndeclare -gA _pgf_args=( '
  for c in "${PGF_CMDS[@]}"; do printf '[%s]=%q ' "$c" "${PGF_CMD_ARGS[$c]}"; done
  for c in "${!PGF_CMD_ALIAS[@]}"; do printf '[%s]=%q ' "$c" "${PGF_CMD_ARGS[${PGF_CMD_ALIAS[$c]}]}"; done
  printf ')\ndeclare -gA _pgf_optval=( '
  for c in "${!PGF_OPT_VALUE[@]}"; do printf '[%q]=%q ' "$c" "${PGF_OPT_VALUE[$c]}"; done
  printf ')\n_pgf_keys=%q\n' "${PGF_CONF_KEYS[*]}"
  printf '_pgf_topics=%q\n' "${PGF_TOPICS[*]}"
  cat <<'EOF'

# _pgf_values KIND PGF CUR — candidate words for one value kind.
_pgf_values() {
  case "$1" in
  build) "$2" __complete builds 2>/dev/null ;;
  tier) "$2" __complete tiers 2>/dev/null; echo all ;;
  tutorial) "$2" __complete tutorials 2>/dev/null ;;
  build-options) "$2" __complete build-options 2>/dev/null ;;
  mpi) echo auto loopback default ;;
  yade) echo 0 1 ;;
  shell) echo bash ;;
  esac
}

# _pgf_setting PGF CUR — KEY= first, then the key's values after the '='.
_pgf_setting() {
  local key="${2%%=*}" val
  if [[ "$2" != *=* ]]; then
    compopt -o nospace 2>/dev/null
    COMPREPLY=($(compgen -W "$_pgf_keys" -- "$2" | sed 's/$/=/'))
    return
  fi
  val="${2#*=}"
  case "$key" in
  PGF_MPI) COMPREPLY=($(compgen -W "auto loopback default" -- "$val")) ;;
  PGF_YADE) COMPREPLY=($(compgen -W "0 1" -- "$val")) ;;
  PGF_BUILD) COMPREPLY=($(compgen -W "$(_pgf_values build "$1")" -- "$val")) ;;
  *) COMPREPLY=($(compgen -f -- "$val")) ;;
  esac
  # Without '=' in COMP_WORDBREAKS the word bash replaces is KEY=VALUE.
  [[ "$COMP_WORDBREAKS" == *=* ]] || COMPREPLY=("${COMPREPLY[@]/#/$key=}")
}

_pgf() {
  local line="${COMP_LINE:0:COMP_POINT}" w=() pgf="${COMP_WORDS[0]}" sub cur prev kind i pos=()
  read -ra w <<<"$line"
  [[ "$line" == *[[:space:]] ]] && w+=("")
  cur="${w[${#w[@]} - 1]}" prev="${w[${#w[@]} - 2]:-}"
  COMPREPLY=()
  if [ ${#w[@]} -le 2 ]; then
    COMPREPLY=($(compgen -W "$_pgf_cmds" -- "$cur"))
    return
  fi
  sub="${w[1]}"
  [ -n "${_pgf_args[$sub]+x}" ] || return
  # After "exec ... --": a command, then its arguments.
  for ((i = 2; i < ${#w[@]} - 1; i++)); do
    [ "${w[i]}" = -- ] || continue
    if [ "$i" -eq $((${#w[@]} - 2)) ]; then COMPREPLY=($(compgen -c -- "$cur")); else COMPREPLY=($(compgen -f -- "$cur")); fi
    return
  done
  kind="${_pgf_optval[$prev]:-}"
  [ "$prev" = --set ] && kind=setting
  case "$kind" in
  none) return ;;
  file) COMPREPLY=($(compgen -f -- "$cur")); return ;;
  dir) COMPREPLY=($(compgen -d -- "$cur")); return ;;
  setting) _pgf_setting "$pgf" "$cur"; return ;;
  "") ;;
  *) COMPREPLY=($(compgen -W "$(_pgf_values "$kind" "$pgf")" -- "$cur")); return ;;
  esac
  if [[ "$cur" == -* ]]; then
    COMPREPLY=($(compgen -W "${_pgf_opts[$sub]}" -- "$cur"))
    [ "$sub" = build ] && COMPREPLY+=($(compgen -W "$(_pgf_values build-options "$pgf")" -- "$cur"))
    return
  fi
  # Positional words so far, skipping options and their values.
  for ((i = 2; i < ${#w[@]} - 1; i++)); do
    case "${w[i]}" in
    -*) [ -n "${_pgf_optval[${w[i]}]+x}" ] && i=$((i + 1)) ;;
    *) pos+=("${w[i]}") ;;
    esac
  done
  case "${_pgf_args[$sub]}" in
  config)
    case "${pos[0]:-}" in
    "") COMPREPLY=($(compgen -W "get set unset" -- "$cur")) ;;
    get | unset) [ ${#pos[@]} -eq 1 ] && COMPREPLY=($(compgen -W "$_pgf_keys" -- "$cur")) ;;
    set) [ ${#pos[@]} -eq 1 ] && _pgf_setting "$pgf" "$cur" ;;
    esac
    ;;
  case)
    if [ ${#pos[@]} -eq 0 ]; then
      compopt -o filenames 2>/dev/null
      COMPREPLY=($(compgen -d -- "$cur"))
    elif [ ${#pos[@]} -eq 1 ]; then
      COMPREPLY=($(cd "${pos[0]}" 2>/dev/null && compgen -W "Allrun Allclean $(find . -maxdepth 1 -type f -perm -u+x -printf '%f ' 2>/dev/null)" -- "$cur"))
    fi
    ;;
  dir) [ ${#pos[@]} -eq 0 ] && COMPREPLY=($(compgen -d -- "$cur")) ;;
  cmd) [ ${#pos[@]} -eq 0 ] && COMPREPLY=($(compgen -W "$_pgf_cmds $_pgf_topics" -- "$cur")) ;;
  build-options) COMPREPLY=($(compgen -W "$(_pgf_values build-options "$pgf")" -- "$cur")) ;;
  dem)
    if [ ${#pos[@]} -eq 0 ]; then COMPREPLY=($(compgen -W run -- "$cur")); else
      compopt -o filenames 2>/dev/null
      COMPREPLY=($(compgen -f -X '!*.py' -- "$cur") $(compgen -d -- "$cur"))
    fi
    ;;
  build | tier | shell | tutorial) [ ${#pos[@]} -eq 0 ] && COMPREPLY=($(compgen -W "$(_pgf_values "${_pgf_args[$sub]}" "$pgf")" -- "$cur")) ;;
  esac
}
complete -F _pgf pgf ./pgf
EOF
}

# pgf_complete_cmd KIND — dynamic completion values, one per line.
pgf_complete_cmd() {
  local f root
  case "${1:-}" in
  builds)
    pgf_conf_resolve 2>/dev/null || return 0
    root="${PGF_BUILD_ROOT:-}"
    if [ -z "$root" ]; then pgf_build_info; root="$PGF_INFO_ROOT"; fi
    echo auto
    for f in "$root"/*/.pgf-build.json; do [ -f "$f" ] && f="${f%/*}" && echo "${f##*/}"; done
    git -C "$PGF_ROOT" worktree list --porcelain 2>/dev/null | sed -n 's|^worktree .*/||p'
    ;;
  tutorials) pgf_tutorials ;;
  tiers)
    sed -n 's/^pgf_tier[[:space:]]\{1,\}\([^[:space:]]*\).*/\1/p' \
      "${PGF_TIERS_CONF:-$PGF_ROOT/applications/test/tiers.conf}" 2>/dev/null
    ;;
  build-options)
    echo build clean
    "$PGF_LIB/build.sh" --help 2>/dev/null | grep -oE -- '--[A-Za-z][A-Za-z-]*' | sort -u
    ;;
  *) return 2 ;;
  esac
}

# pgf_completion_file — where bash-completion looks for pgf's script.
pgf_completion_file() {
  printf '%s/completions/pgf' "${BASH_COMPLETION_USER_DIR:-${XDG_DATA_HOME:-${HOME:-}/.local/share}/bash-completion}"
}

# pgf_install_completion ASK — write the script there; ASK=1 asks first.
pgf_install_completion() {
  local file ans
  file="$(pgf_completion_file)"
  if [ "$1" -eq 1 ]; then
    printf 'install bash completion to %s? [Y/n] ' "$file" >&2
    read -r ans </dev/tty || ans=""
    case "$ans" in [nN]*) echo "pgf: skipped; later: pgf completion bash > $file" >&2; return 0 ;; esac
  fi
  mkdir -p "${file%/*}" && pgf_completion_cmd bash >"$file" || { echo "pgf: cannot write $file" >&2; return 2; }
  PGF_COMPLETION_INSTALLED=1
  [ "${PGF_SETUP_QUIET:-0}" -eq 1 ] ||
    echo "pgf: bash completion in $file (bash-completion loads it; else source it from ~/.bashrc)" >&2
}
