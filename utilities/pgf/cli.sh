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
  [--keep]=none [--keep-runs]=none [--keep-prefixes]=none [--if-stale]=none [-c]=none)

# The environment options of exec, shell, run, status and doctor.
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

pgf_cmd status "settings, where each came from, and this checkout's builds" "pgf status [PATH] [--json] [options]" "--json @env" dir
pgf_cmd_desc status "Prints every setting with the layer it came from, then this checkout's two
builds (yade0 plain, yade1 DEM) and whether each is built. With PATH, asks
that checkout instead. pgf with no command runs status."
pgf_cmd_opt status --json "settings, layers and builds as JSON"
pgf_cmd_opt status @env
pgf_cmd_ex status "pgf status"
pgf_cmd_ex status "pgf status ~/Code/pgf-feature" "another checkout's settings and builds"

pgf_cmd where "where pgf keeps settings, builds, records and runs" "pgf where" "" none
pgf_cmd_desc where "One line each: the settings files, the build store, the run records, the run
dir, and the build pgf would use now with the layer that chose it."
pgf_cmd_ex where "pgf where"

pgf_cmd doctor "status plus checks, each with a fix" "pgf doctor [options]" "@env" none
pgf_cmd_desc doctor "Prints status, then read-only checks: the OpenFOAM version, a build for this
checkout, yade and the Foam-Yade pin, MPI, and pgf on PATH. Each check is one
OK, WARN or FAIL line with a fix. Exits 1 when a check fails."
pgf_cmd_opt doctor @env
pgf_cmd_ex doctor "pgf doctor"
pgf_cmd_ex doctor "pgf doctor --yade" "also check yade and the coupling"

pgf_cmd setup "find OpenFOAM and yade, save them, put pgf on PATH" \
  "pgf setup [--yes] [--user | --checkout] [--no-link | --link-only] [--force] [--no-completion]" \
  "--user --checkout --yes --no-link --link-only --force --no-completion --set --foam-bashrc --yade-bin --yade-trunk" none
pgf_cmd_desc setup "pgf runs every build and case in a clean environment, so it has to know where
OpenFOAM (and, for DEM cases, yade) lives. setup finds them, saves them in a
settings file, links ~/.local/bin/pgf to this checkout and installs bash
completion. On a terminal it asks when there is a choice; --yes takes the
detected choices without asking. Rerun it any time: it keeps the values you set
and says what changed. Change a value later with pgf config set KEY=VALUE."
pgf_cmd_opt setup "-y, --yes" "do not ask: take what was detected, install completion"
pgf_cmd_opt setup --user "save to ~/.config/pgf/env.conf, for every checkout" "yes"
pgf_cmd_opt setup --checkout "save to <checkout>/.pgf.conf, for this checkout only"
pgf_cmd_opt setup --no-link "do not link ~/.local/bin/pgf"
pgf_cmd_opt setup --link-only "only link ~/.local/bin/pgf to this checkout"
pgf_cmd_opt setup --force "replace a link that points at another checkout"
pgf_cmd_opt setup --no-completion "do not install bash completion"
pgf_cmd_opt setup "--foam-bashrc FILE" "this OpenFOAM etc/bashrc instead of a detected one"
pgf_cmd_opt setup "--yade-bin FILE" "this yade binary instead of a detected one"
pgf_cmd_opt setup "--yade-trunk DIR" "this Foam-Yade source tree"
pgf_cmd_opt setup "--set KEY=VALUE" "save any setting (keys: pgf help config)"
pgf_cmd_ex setup "./pgf setup" "ask, then save and link"
pgf_cmd_ex setup "./pgf setup --yes" "take the detected choices"
pgf_cmd_ex setup "./pgf setup --foam-bashrc ~/OpenFOAM/OpenFOAM-v2406/etc/bashrc"
pgf_cmd_ex setup "./pgf setup --link-only --force" "make pgf on PATH this checkout's"

pgf_cmd config "show every setting with its layer, or change one" \
  "pgf config [get KEY | set KEY=VALUE | unset KEY] [--user | --checkout] [--json]" "--user --checkout --json" config
pgf_cmd_desc config "Each setting comes from, later winning: the default, ~/.config/pgf/env.conf
(user layer), <checkout>/.pgf.conf (checkout layer), the environment, the
command line. Without a verb, config prints every setting and its layer; set
and unset edit the user file, or the checkout file with --checkout."
pgf_cmd_opt config --user "edit ~/.config/pgf/env.conf" "yes"
pgf_cmd_opt config --checkout "edit <checkout>/.pgf.conf"
pgf_cmd_opt config --json "print the settings and layers as JSON"
pgf_cmd_ex config "pgf config"
pgf_cmd_ex config "pgf config set PGF_FOAM_BASHRC=~/OpenFOAM/OpenFOAM-v2406/etc/bashrc"
pgf_cmd_ex config "pgf config set PGF_MPI=loopback --checkout" "this checkout only"
pgf_cmd_ex config "pgf config get YADE_BIN"

pgf_cmd build "build this checkout (libraries, solver, utilities)" "pgf build [--yade] [-v] [build.sh options]" \
  "--yade --set --foam-bashrc" build-options
pgf_cmd_desc build "Builds this checkout into the build store, in a prefix named by a key made of
the sources, the variant, the OpenFOAM version and the compiler. A prefix with
the same key is reused without compiling. wmake's output goes to
<prefix>/build.log; the screen shows one line per target. ./Allwmake runs this."
pgf_cmd_opt build --yade "the DEM variant (WITH_YADE=1)"
pgf_cmd_opt build "-v, --verbose" "stream wmake's output instead of logging it"
pgf_cmd_opt build --libs-only "only the libraries"
pgf_cmd_opt build --dry-run "show what would be built; build nothing"
pgf_cmd_opt build --print-prefix "print the prefix this checkout builds into"
pgf_cmd_opt build "clean [--purge]" "clean the build state (and the binaries)"
pgf_cmd_ex build "pgf build"
pgf_cmd_ex build "pgf build --yade" "the DEM variant"
pgf_cmd_ex build "utilities/pgf/build.sh --help" "every build.sh option"

pgf_cmd builds "every build: key, variant, age, size, checkout, in use" "pgf builds [--json]" "--json" none
pgf_cmd_desc builds "Lists every build in the build store; * marks this checkout's."
pgf_cmd_opt builds --json "as JSON"
pgf_cmd_ex builds "pgf builds"

pgf_cmd ls "registered checkouts, and every build" "pgf ls [--json]" "--json" none
pgf_cmd_desc ls "Lists the checkouts that built into the build store, then every build."
pgf_cmd_opt ls --json "as JSON"
pgf_cmd_ex ls "pgf ls"

pgf_cmd use "choose this terminal's build" "eval \"\$(pgf use <key|commit|worktree|prefix|auto> [--yade])\"" \
  "--yade --no-yade" build
pgf_cmd_desc use "Prints the shell line that makes a build this terminal's: wrap it in eval.
Inside pgf shell, plain pgf use does the eval. auto goes back to the build
of this checkout's sources."
pgf_cmd_opt use --yade "pick from the DEM builds"
pgf_cmd_opt use --no-yade "pick from the plain builds"
pgf_cmd_ex use 'eval "$(pgf use a2055)"' "a key prefix"
pgf_cmd_ex use 'eval "$(pgf use auto)"'

pgf_cmd gc "remove old unused builds" "pgf gc [--keep N] [--yes] [--dry-run] [--json]" "--keep --yes --dry-run --json" none
pgf_cmd_desc gc "Removes builds that no checkout last built, that are not in use or being
built, beyond the newest N."
pgf_cmd_opt gc "--keep N" "keep the N newest" 2
pgf_cmd_opt gc --yes "remove without asking"
pgf_cmd_opt gc --dry-run "only list what would go"
pgf_cmd_opt gc --json "as JSON"
pgf_cmd_ex gc "pgf gc --dry-run"

pgf_cmd pack "an Apptainer image of a commit" "pgf pack [BUILD] [--no-yade] [--output FILE] [--dry-run]" \
  "--yade --no-yade --output --dry-run" build
pgf_cmd_desc pack "Builds a self-contained Apptainer image of one commit: the image compiles the
pinned Foam-Yade and PGF itself. BUILD (a key, commit or worktree) names the
commit; the default is this checkout's HEAD, which must be clean."
pgf_cmd_opt pack --no-yade "the plain variant (no yade in the image)"
pgf_cmd_opt pack "--output FILE" "the .sif to write" "./pgf-<commit12>-yade<0|1>.sif"
pgf_cmd_opt pack --dry-run "write the build context and print the definition"
pgf_cmd_ex pack "pgf pack --dry-run"

pgf_cmd exec "run a command in the clean environment with the build" "pgf exec [options] -- <command ...>" "@env --" command
pgf_cmd_desc exec "Runs the command with the caller's environment dropped (a small allowlist
kept), OpenFOAM's bashrc sourced and the user dirs pointed at the build.
A build that is not this checkout's sources is stale: exec warns."
pgf_cmd_opt exec @env
pgf_cmd_ex exec "pgf exec -- porousGasificationFoam -help"
pgf_cmd_ex exec "pgf exec --yade -- yade -v"

pgf_cmd shell "an interactive shell in that environment" "pgf shell [options] [-c COMMAND]" "@env -c" none
pgf_cmd_desc shell "Starts bash in the same environment as exec, with the prompt
(pgf:<worktree>@<key>). pgf calls inside it keep that environment."
pgf_cmd_opt shell "-c COMMAND" "run COMMAND instead of an interactive shell"
pgf_cmd_opt shell @env
pgf_cmd_ex shell "pgf shell"
pgf_cmd_ex shell "pgf shell -c 'command -v porousGasificationFoam'"

pgf_cmd run "run a case script and record the run" \
  "pgf run [options] [--build] [--allow-stale] <caseDir> [Allrun | Allclean | script] [args ...]" \
  "@env --build --allow-stale" case
pgf_cmd_desc run "Changes into caseDir and runs the script (default Allrun) in the clean
environment, then writes <caseDir>/.pgf-run.json and says how it went. A case
whose constant/yadeProperties is active gets --yade. Run a copy of a tutorial,
not the tutorial in the checkout. A stale build is refused."
pgf_cmd_opt run --build "build this checkout first"
pgf_cmd_opt run --allow-stale "run with a build of other sources"
pgf_cmd_opt run @env
pgf_cmd_ex run "cp -rL tutorials/cases/solidAdvection ~/run/ && pgf run ~/run/solidAdvection"
pgf_cmd_ex run "pgf run ~/run/solidAdvection Allclean"

pgf_cmd test "run a test tier against this checkout's build" \
  "pgf test <tier|all> [--rebuild] [--if-stale AGE] [--run-root DIR] | --status | --gc" \
  "--rebuild --if-stale --run-root --status --gc --keep-runs --keep-prefixes --dry-run" tier
pgf_cmd_desc test "Builds (or reuses) the tier's build, runs its cases and records the outcome
in ~/.local/state/pgf/runs/last/<tier>.json. Tiers are in
applications/test/tiers.conf: fast, convergence, dem. Exit 0 PASS, 1 FAIL,
2 ERROR, 124 TIMEOUT."
pgf_cmd_opt test --rebuild "delete the tier's build first, then build it again"
pgf_cmd_opt test "--if-stale AGE" "run only when the last PASS is older than AGE or of other sources"
pgf_cmd_opt test "--run-root DIR" "the run dir" '$PGF_RUN_ROOT/pgf-test-<tier>-<time>'
pgf_cmd_opt test --status "per tier: last outcome, when, and whether it is current"
pgf_cmd_opt test --gc "prune old run dirs and builds"
pgf_cmd_opt test "--keep-runs N" "with --gc: run dirs to keep" 10
pgf_cmd_opt test "--keep-prefixes M" "with --gc: builds to keep" 5
pgf_cmd_opt test --dry-run "with --gc: only list what would go"
pgf_cmd_ex test "pgf test fast"
pgf_cmd_ex test "pgf test fast --if-stale 7d"
pgf_cmd_ex test "pgf test --status"

pgf_cmd tools "every tool at the checkout root: what, why, use instead" "pgf tools" "" none
pgf_cmd_desc tools "For each executable at the checkout root, what it does, why it exists and
what to use instead."
pgf_cmd_ex tools "pgf tools"

pgf_cmd completion "print the bash completion script" "pgf completion bash" "" shell
pgf_cmd_desc completion "Prints the bash completion script; pgf setup installs it."
pgf_cmd_ex completion "pgf completion bash > ~/.local/share/bash-completion/completions/pgf"

pgf_cmd help "help about a command" "pgf help [command]" "" cmd
pgf_cmd_desc help "Prints the help of pgf, or of one command; pgf <command> --help is the same."
pgf_cmd_ex help "pgf help setup"

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
  depends on what your shell has sourced or on a shared $FOAM_USER_APPBIN.
  Settings come from, later winning: the default, ~/.config/pgf/env.conf,
  <checkout>/.pgf.conf, the environment, the command line (pgf help config).
  If you only need the OpenFOAM-style entry point, ./Allwmake and ./Allwclean
  build and clean through pgf.
EOF
  _pgf_help_section "Available Commands"
  for c in "${PGF_CMDS[@]}"; do rows+="$c"$'\t'"${PGF_CMD_SUM[$c]}"$'\n'; done
  _pgf_help_rows "$rows"
  _pgf_help_section Options
  _pgf_help_rows "--color WHEN	colour: auto (on a terminal, unless NO_COLOR is set), always, never	auto
-h, --help	this help, or a command's"
  _pgf_help_section Examples
  _pgf_help_examples "./pgf setup	find OpenFOAM, save it, put pgf on PATH
./Allwmake	build this checkout (pgf build)
pgf test fast	the fast test tier
pgf run ~/run/solidAdvection	run a copied tutorial case
pgf doctor	check the setup, with fixes"
  echo
  echo "Use \"pgf help <command>\" or \"pgf <command> --help\" for more about a command."
}

# pgf_cli_help_cmd CMD — one command's page: Usage, Description, Options, Examples.
pgf_cli_help_cmd() {
  local c="$1" rows="" line
  [ -n "${PGF_CMD_SUM[$c]+x}" ] || return 2
  echo "pgf $c: ${PGF_CMD_SUM[$c]}"
  _pgf_help_section Usage
  echo "  ${PGF_CMD_USE[$c]}"
  _pgf_help_section Description
  sed 's/^/  /' <<<"${PGF_CMD_DESC[$c]}"
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
  echo "# Dynamic values come from: pgf __complete builds|tiers|build-options"
  printf '_pgf_cmds=%q\n' "${PGF_CMDS[*]}"
  printf 'declare -gA _pgf_opts=( '
  for c in "${PGF_CMDS[@]}"; do printf '[%s]=%q ' "$c" "$(pgf_cli_opts "$c")"; done
  printf ')\ndeclare -gA _pgf_args=( '
  for c in "${PGF_CMDS[@]}"; do printf '[%s]=%q ' "$c" "${PGF_CMD_ARGS[$c]}"; done
  printf ')\ndeclare -gA _pgf_optval=( '
  for c in "${!PGF_OPT_VALUE[@]}"; do printf '[%q]=%q ' "$c" "${PGF_OPT_VALUE[$c]}"; done
  printf ')\n_pgf_keys=%q\n' "${PGF_CONF_KEYS[*]}"
  cat <<'EOF'

# _pgf_values KIND PGF CUR — candidate words for one value kind.
_pgf_values() {
  case "$1" in
  build) "$2" __complete builds 2>/dev/null ;;
  tier) "$2" __complete tiers 2>/dev/null; echo all ;;
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
  cmd) [ ${#pos[@]} -eq 0 ] && COMPREPLY=($(compgen -W "$_pgf_cmds" -- "$cur")) ;;
  build-options) COMPREPLY=($(compgen -W "$(_pgf_values build-options "$pgf")" -- "$cur")) ;;
  build | tier | shell) [ ${#pos[@]} -eq 0 ] && COMPREPLY=($(compgen -W "$(_pgf_values "${_pgf_args[$sub]}" "$pgf")" -- "$cur")) ;;
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
