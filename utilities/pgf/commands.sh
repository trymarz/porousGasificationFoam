#!/bin/bash
# commands.sh — pgf's setup / config / build / doctor helpers (#257, #264).
# Source-only; needs config.sh, PGF_ROOT, SELF and PGF_ENV_CALLER_PATH.

# pgf_case_wants_yade DIR — DIR/constant/yadeProperties says active true.
pgf_case_wants_yade() {
  grep -qE '^[[:space:]]*active[[:space:]]+(true|yes|on|1)[[:space:]]*;' \
    "$1/constant/yadeProperties" 2>/dev/null
}

# pgf_detect_bashrc — candidate OpenFOAM etc/bashrc files, v2406 first.
pgf_detect_bashrc() {
  local c
  {
    [ -n "${WM_PROJECT_DIR:-}" ] && echo "$WM_PROJECT_DIR/etc/bashrc"
    for c in "$HOME"/OpenFOAM/*/etc/bashrc /opt/openfoam*/etc/bashrc \
      /usr/lib/openfoam/*/etc/bashrc; do
      echo "$c"
    done
  } | while IFS= read -r c; do [ -r "$c" ] && echo "$c"; done |
    awk '!seen[$0]++' | awk '/2406/ {print; next} {rest[++n]=$0} END {for (i=1;i<=n;i++) print rest[i]}'
}

# pgf_detect_yade — yade binaries on the caller's PATH, plain name first.
pgf_detect_yade() {
  local d f
  local IFS=:
  for d in ${PGF_ENV_CALLER_PATH:-}; do
    for f in "$d"/yade "$d"/yade-* "$d"/yade[0-9]*; do
      case "$f" in *.py | *-batch | *batch*) continue ;; esac
      [ -f "$f" ] && [ -x "$f" ] && echo "$f"
    done
  done | awk '!seen[$0]++'
}

# pgf_pick KEY CANDIDATES... — first candidate, or ask when interactive.
pgf_pick() {
  local key="$1" ans
  shift
  if [ "$PGF_INIT_ASK" -eq 0 ] || [ $# -le 1 ]; then
    printf '%s' "${1:-}"
    return
  fi
  {
    echo "$key candidates:"
    local i=1 c
    for c in "$@"; do echo "  $i) $c"; i=$((i + 1)); done
    printf 'choose 1-%d, a path, or empty for 1: ' "$#"
  } >&2
  read -r ans </dev/tty || ans=""
  case "$ans" in
  "") printf '%s' "$1" ;;
  *[!0-9]*) printf '%s' "$ans" ;;
  *) local -a all=("$@"); printf '%s' "${all[$((ans - 1))]:-$1}" ;;
  esac
}

# pgf_init LAYER YES — merge CLI values (else detected ones) into the file.
pgf_init() {
  local layer="$1" file k v
  PGF_INIT_ASK=0
  [ "$2" -eq 0 ] && [ -t 0 ] && [ -r /dev/tty ] && PGF_INIT_ASK=1
  if [ "$layer" = "checkout" ]; then file="$(pgf_conf_checkout_file)"; else file="$(pgf_conf_user_file)"; fi

  PGF_CONF_VAL=() PGF_CONF_SRC=()
  pgf_conf_parse "$([ "$layer" = checkout ] && pgf_conf_checkout_read_file || echo "$file")" "$layer" || { echo "pgf: $PGF_CONF_ERR" >&2; return 1; }
  local -A out=()
  for k in "${!PGF_CONF_VAL[@]}"; do out[$k]="${PGF_CONF_VAL[$k]}"; done

  local -a cands
  if [ -z "${PGF_CONF_CLI[PGF_FOAM_BASHRC]+x}" ] && [ -z "${out[PGF_FOAM_BASHRC]+x}" ]; then
    mapfile -t cands < <(pgf_detect_bashrc)
    v="$(pgf_pick PGF_FOAM_BASHRC "${cands[@]}")"
    [ -n "$v" ] && out[PGF_FOAM_BASHRC]="$v"
  fi
  if [ -z "${PGF_CONF_CLI[YADE_BIN]+x}" ] && [ -z "${out[YADE_BIN]+x}" ]; then
    mapfile -t cands < <(pgf_detect_yade)
    v="$(pgf_pick YADE_BIN "${cands[@]}")"
    [ -n "$v" ] && out[YADE_BIN]="$v"
  fi
  if [ "$layer" = "checkout" ] && [ -z "${out[YADE_TRUNK]+x}" ] &&
    [ -d "$PGF_ROOT/submodules/Foam-Yade/pkg/openfoam" ]; then
    out[YADE_TRUNK]="$PGF_ROOT/submodules/Foam-Yade"
  fi
  for k in "${!PGF_CONF_CLI[@]}"; do out[$k]="${PGF_CONF_CLI[$k]}"; done

  # Remember old and new values for pgf_setup_summary; leave an unchanged file alone.
  PGF_SETUP_LAYER="$layer" PGF_SETUP_FILE="$file" PGF_SETUP_WROTE=0
  PGF_SETUP_OLD=() PGF_SETUP_NEW=()
  for k in "${!PGF_CONF_VAL[@]}"; do PGF_SETUP_OLD[$k]="${PGF_CONF_VAL[$k]}"; done
  for k in "${!out[@]}"; do PGF_SETUP_NEW[$k]="${out[$k]}"; done
  if [ -e "$file" ] && pgf_assoc_equal PGF_SETUP_OLD PGF_SETUP_NEW; then return 0; fi
  pgf_conf_write "$file" "$layer" out || return 1
  PGF_SETUP_WROTE=1
}

declare -gA PGF_SETUP_OLD=() PGF_SETUP_NEW=()

# pgf_assoc_equal A B — the two named associative arrays hold the same pairs.
pgf_assoc_equal() {
  local -n _a="$1" _b="$2"
  local k
  [ "${#_a[@]}" -eq "${#_b[@]}" ] || return 1
  for k in "${!_a[@]}"; do
    [ -n "${_b[$k]+x}" ] && [ "${_b[$k]}" = "${_a[$k]}" ] || return 1
  done
}

# pgf_foam_version BASHRC — WM_PROJECT_VERSION after sourcing BASHRC in a child.
pgf_foam_version() {
  local info
  info="$(env -i HOME="$HOME" PATH=/usr/bin:/bin bash -c 'f="$1"; set +eu; set --; . "$f" >/dev/null 2>&1; echo "$WM_PROJECT_VERSION"' _ "$1")"
  printf '%s' "${info##*$'\n'}"
}

# pgf_tilde PATH — PATH with $HOME shown as ~.
pgf_tilde() {
  case "$1" in "$HOME"/*) printf '~/%s' "${1#"$HOME"/}" ;; *) printf '%s' "$1" ;; esac
}

# pgf_setup_summary LINKED COMPLETION — what pgf setup decided, after pgf_init:
#   one "OK|WARN|FAIL message" line per decision, then the next steps.
pgf_setup_summary() {
  local linked="$1" completion="$2" k old new bashrc ver yb p link="$HOME/.local/bin/pgf"
  _s() { printf '  %-4s %s\n' "$1" "$2"; }
  pgf_conf_resolve 2>/dev/null || true
  echo
  echo "pgf setup summary"
  if [ "$PGF_SETUP_WROTE" -eq 1 ]; then
    _s OK "settings: wrote $(pgf_tilde "$PGF_SETUP_FILE") ($PGF_SETUP_LAYER layer)"
  else
    _s OK "settings: $(pgf_tilde "$PGF_SETUP_FILE") unchanged ($PGF_SETUP_LAYER layer)"
  fi
  for k in "${PGF_CONF_KEYS[@]}"; do
    [ -n "${PGF_SETUP_NEW[$k]+x}" ] || continue
    new="${PGF_SETUP_NEW[$k]}"
    if [ -z "${PGF_SETUP_OLD[$k]+x}" ]; then
      printf '         %-16s %s (new)\n' "$k" "$(pgf_tilde "$new")"
    elif [ "${PGF_SETUP_OLD[$k]}" = "$new" ]; then
      printf '         %-16s %s (kept)\n' "$k" "$(pgf_tilde "$new")"
    else
      old="${PGF_SETUP_OLD[$k]}"
      printf '         %-16s %s (was %s)\n' "$k" "$(pgf_tilde "$new")" "$(pgf_tilde "$old")"
    fi
  done

  bashrc="${PGF_FOAM_BASHRC:-}"
  if [ -z "$bashrc" ] || [ ! -r "$bashrc" ]; then
    _s FAIL "no readable OpenFOAM bashrc (${bashrc:-none found}); fix: pgf config set PGF_FOAM_BASHRC=<OpenFOAM-v2406>/etc/bashrc"
  else
    ver="$(pgf_foam_version "$bashrc")"
    if [ "$ver" = v2406 ]; then
      _s OK "OpenFOAM $ver: $(pgf_tilde "$bashrc")"
    else
      _s WARN "OpenFOAM ${ver:-version unknown}: $(pgf_tilde "$bashrc"); PGF targets v2406"
      _s "" "fix: pgf config set PGF_FOAM_BASHRC=<OpenFOAM-v2406>/etc/bashrc"
    fi
  fi

  yb="${YADE_BIN:-}"
  if [ -n "$yb" ]; then
    _s OK "yade: $(pgf_tilde "$yb")"
  else
    _s OK "yade: not found; DEM coupling (--yade) unavailable, the CFD solver is unaffected"
  fi

  p="$(PATH="$PGF_ENV_CALLER_PATH" command -v pgf)"
  if [ -n "$p" ] && [ "$(readlink -f "$p")" = "$SELF" ]; then
    _s OK "pgf on PATH is this checkout's"
  elif [ -n "$p" ]; then
    _s WARN "pgf on PATH is $(readlink -f "$p"), not this checkout; call this one by path"
  elif [ "$(readlink -f "$link")" = "$SELF" ]; then
    _s WARN "$(pgf_tilde "$link") is set, but ~/.local/bin is not on your PATH; fix:"
    _s "" 'export PATH="$HOME/.local/bin:$PATH"   (add it to ~/.bashrc)'
  elif [ "$linked" -eq 0 ]; then
    _s OK "pgf not linked (--no-link); call $(pgf_tilde "$SELF") by path"
  else
    _s WARN "$(pgf_tilde "$link") does not point here; fix: $(pgf_tilde "$SELF") setup --link-only --force"
  fi

  if [ "$completion" -eq 1 ]; then
    _s OK "bash completion installed; takes effect in a new shell"
  fi
  echo "next:"
  echo "  pgf config     every setting and the layer it comes from"
  echo "  pgf status     settings plus the builds for this checkout"
  echo "  ./Allwmake     build the solver here (≈1.5 min first build)"
}

# pgf_install_link FORCE — ~/.local/bin/pgf -> this checkout's pgf.
pgf_install_link() {
  local link="$HOME/.local/bin/pgf" cur
  if [ -L "$link" ] || [ -e "$link" ]; then
    cur="$(readlink -f "$link")"
    if [ "$cur" = "$SELF" ]; then
      echo "pgf: $link already points here" >&2
      return 0
    fi
    [ "$1" -eq 1 ] || {
      echo "pgf: $link points at ${cur:-a file}; rerun with --force to replace it" >&2
      echo "pgf: (with several worktrees, the link picks one; call the others by path)" >&2
      return 2
    }
  fi
  mkdir -p "${link%/*}" && ln -sfn "$SELF" "$link" || return 2
  echo "pgf: $link -> $SELF" >&2
  case ":$PGF_ENV_CALLER_PATH:" in
  *":${link%/*}:"*) ;;
  *) echo "pgf: note: ${link%/*} is not on your PATH" >&2 ;;
  esac
}

# pgf_doctor — read-only checks, one "OK|WARN|FAIL message" line each; returns
#   1 when any FAIL. Runs before OpenFOAM is sourced, so it sources in a child.
pgf_doctor() {
  local fails=0 bashrc ver v p yb out pin trunk why
  _r() {
    printf '  %-4s %s\n' "$1" "$2"
    [ "$1" = FAIL ] && fails=$((fails + 1))
    return 0
  }
  echo "checks:"
  bashrc="${PGF_FOAM_BASHRC:-${WM_PROJECT_DIR:+$WM_PROJECT_DIR/etc/bashrc}}"
  if [ -n "$bashrc" ] && [ -r "$bashrc" ]; then
    ver="$(pgf_foam_version "$bashrc")"
    if [ -z "$ver" ]; then
      _r FAIL "$bashrc set no WM_PROJECT_VERSION; fix: pgf config set PGF_FOAM_BASHRC=<OpenFOAM-v2406>/etc/bashrc"
    elif [ "$ver" != v2406 ]; then
      _r WARN "OpenFOAM $ver from $bashrc; the solver targets v2406"
    else
      _r OK "OpenFOAM $ver from $bashrc"
    fi
    for v in 0 1; do
      [ "$v" -eq 1 ] && [ "${PGF_YADE:-0}" != 1 ] && continue
      local flag=()
      [ "$v" -eq 1 ] && flag=(--yade)
      p="$("$SELF" exec --bare --mpi default -- "$PGF_LIB/build.sh" --print-prefix "${flag[@]}" 2>/dev/null | tail -1)"
      if [ -n "$p" ] && [ -f "$p/.pgf-build.json" ]; then
        _r OK "yade$v build present: $p"
      else
        _r WARN "no yade$v build for this tree${p:+ at $p}; fix: $SELF build${flag:+ --yade}"
      fi
    done
  else
    _r FAIL "no readable OpenFOAM bashrc (${bashrc:-unset}); fix: pgf setup, or pgf config set PGF_FOAM_BASHRC=FILE"
  fi

  yb="${YADE_BIN:-$(PATH="$PGF_ENV_CALLER_PATH" command -v yade)}"
  if [ -z "$yb" ] && [ "${PGF_YADE:-0}" != 1 ]; then
    _r OK "yade: not configured (only needed for --yade)"
  elif [ ! -x "$yb" ]; then
    _r FAIL "no yade binary (${yb:-none}); fix: pgf config set YADE_BIN=PATH (see doc/build.md, YADE DEM coupling)"
  else
    out="$(timeout 60 "$yb" -v 2>/dev/null | grep -m1 -i 'version')"
    _r OK "yade $yb: ${out:-version unknown}"
    while IFS= read -r line; do
      _r "${line%% *}" "$(sed -E 's/^[A-Z]+ +//' <<<"$line")"
    done < <(pgf_yade_check "$yb" "${YADE_TRUNK:-$PGF_ROOT/submodules/Foam-Yade}")
    if timeout 60 "$yb" --help 2>/dev/null | grep -q 'MPI'; then
      _r OK "yade lists MPI among its compiled features"
    else
      _r WARN "yade --help does not list MPI; the coupling needs ENABLE_MPI"
    fi
  fi

  trunk="${YADE_TRUNK:-$PGF_ROOT/submodules/Foam-Yade}"
  if pgf_coupling_api_hash "$trunk" >/dev/null; then
    local libdir="${PGF_YADE_LIBDIR:-}" line
    if [ -n "$libdir" ]; then
      while IFS= read -r line; do _r "${line%% *}" "${line#* }"; done < <(pgf_coupling_check "$libdir" "$trunk")
    else
      _r OK "Foam-Yade headers at $trunk (set PGF_YADE_LIBDIR to check the coupling libs)"
    fi
  elif [ "${PGF_YADE:-0}" = 1 ]; then
    _r FAIL "no Foam-Yade coupling headers under $trunk; fix: git submodule update --init submodules/Foam-Yade, or set YADE_TRUNK"
  fi

  case "${PGF_MPI:-auto}" in
  auto)
    if why="$(pgf_mpi_auto)"; then _r OK "MPI auto: loopback ($why)"; else _r OK "MPI auto: Open MPI left alone ($why)"; fi
    ;;
  *) _r OK "MPI ${PGF_MPI} (set explicitly); interfaces: $(pgf_mpi_ifaces | tr '\n' ' ')" ;;
  esac

  p="$(PATH="$PGF_ENV_CALLER_PATH" command -v pgf)"
  if [ -z "$p" ]; then
    _r WARN "pgf is not on PATH; fix: $SELF setup --link-only"
  elif [ "$(readlink -f "$p")" != "$SELF" ]; then
    _r WARN "pgf on PATH is $(readlink -f "$p"), not this checkout; call this one by path"
  else
    _r OK "pgf on PATH is this checkout's"
  fi
  [ "$fails" -eq 0 ]
}

# pgf_config_cmd [get KEY | set KEY=VALUE | unset KEY] [--user | --checkout]
#   [--json] — read the layered settings, or edit one settings file.
pgf_config_cmd() {
  local verb="" arg="" layer="user" json=0 file k
  case "${1:-}" in get | set | unset) verb="$1" arg="${2:-}"; shift 2 || shift ;; esac
  while [ $# -gt 0 ]; do
    case "$1" in
    --user | --checkout) layer="${1#--}" ;;
    --json) json=1 ;;
    -h | --help) echo "usage: pgf config [get KEY | set KEY=VALUE | unset KEY] [--user | --checkout] [--json]"; return 0 ;;
    *) echo "pgf config: unexpected '$1'" >&2; return 2 ;;
    esac
    shift
  done
  case "$verb" in
  "")
    pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
    if [ "$json" -eq 1 ]; then pgf_conf_json; else pgf_conf_print; fi
    ;;
  get)
    pgf_conf_known_key "$arg" || { echo "pgf config: unknown key '$arg' (known: ${PGF_CONF_KEYS[*]})" >&2; return 2; }
    pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
    [ -n "${PGF_CONF_VAL[$arg]+x}" ] || return 1
    printf '%s\n' "${PGF_CONF_VAL[$arg]}"
    ;;
  set | unset)
    k="${arg%%=*}"
    if [ "$verb" = set ]; then
      local msg
      msg="$(pgf_conf_set_cli "$arg")" || { echo "pgf config set: ${msg#--set: }" >&2; return 2; }
    else
      pgf_conf_known_key "$k" || { echo "pgf config: unknown key '$k'" >&2; return 2; }
    fi
    [ "$k" != PGF_BUILD ] || [ "$layer" = checkout ] ||
      { echo "pgf config: PGF_BUILD is per worktree (--checkout) or per terminal (pgf use)" >&2; return 2; }
    [ "$layer" = checkout ] && file="$(pgf_conf_checkout_file)" || file="$(pgf_conf_user_file)"
    PGF_CONF_VAL=() PGF_CONF_SRC=()
    pgf_conf_parse "$([ "$layer" = checkout ] && pgf_conf_checkout_read_file || echo "$file")" "$layer" ||
      { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
    local -A out=()
    for k in "${!PGF_CONF_VAL[@]}"; do out[$k]="${PGF_CONF_VAL[$k]}"; done
    k="${arg%%=*}"
    if [ "$verb" = set ]; then out[$k]="${arg#*=}"; else unset "out[$k]"; fi
    pgf_conf_write "$file" "$layer" out || { echo "pgf: cannot write $file" >&2; return 2; }
    echo "pgf: $verb $k in $file" >&2
    ;;
  esac
}

# pgf_build_cmd [--yade] [pgf env options] [build.sh options] — build this tree
#   into the shared store (build.sh --isolated); an identical build is reused.
pgf_build_cmd() {
  local envopts=() bopts=()
  while [ $# -gt 0 ]; do
    case "$1" in
    --yade) envopts+=(--yade); bopts+=(--yade) ;;
    --set | --foam-bashrc | --mpi) envopts+=("$1" "${2:-}"); shift ;;
    -h | --help) echo "usage: pgf build [--yade] [--set K=V] [build.sh options; see utilities/pgf/build.sh --help]"; return 0 ;;
    *) bopts+=("$1") ;;
    esac
    shift
  done
  "$SELF" exec --bare "${envopts[@]}" -- "$PGF_LIB/build.sh" --isolated "${bopts[@]}"
}

# pgf_tools_cmd — the what / why / instead header of every tool at the root.
pgf_tools_cmd() {
  local f
  local -A seen=()
  [ $# -eq 0 ] || { echo "usage: pgf tools" >&2; return 2; }
  echo "tools at $PGF_ROOT (pgf is the front door; the rest are conventions or shims):"
  for f in pgf Allwmake Allwclean "$PGF_ROOT"/*; do
    f="${f##*/}"
    [ -z "${seen[$f]:-}" ] && [ -f "$PGF_ROOT/$f" ] && [ -x "$PGF_ROOT/$f" ] || continue
    seen[$f]=1
    awk -v t="$f" 'NR > 12 { exit }
      /^# (what|why|instead): / { if (!n++) print "\n" t; sub(/^# /, "  "); print; more = 1; next }
      more && /^#   +[^ ]/ { sub(/^# /, "  "); print; next }
      { more = 0 }' "$PGF_ROOT/$f"
  done
}

# pgf_where_cmd — settings files, build store, run records, run dir and the
#   active build with the layer that chose it, one line each.
pgf_where_cmd() {
  local f out=() variant sel src key prefix state
  [ $# -eq 0 ] || { echo "usage: pgf where" >&2; return 2; }
  pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
  pgf_build_info
  for f in "$(pgf_conf_user_file)" "$(pgf_conf_checkout_read_file)"; do
    if [ -e "$f" ]; then out+=("$f"); else out+=("$f (absent)"); fi
  done
  _layer() {
    case "${PGF_CONF_SRC[$1]:-default}" in
    env) echo "${2:-environment}" ;;
    cli) echo "command line" ;;
    default) echo default ;;
    *) echo "${PGF_CONF_SRC[$1]}" ;;
    esac
  }
  variant="yade${PGF_YADE:-0}" sel="${PGF_BUILD:-auto}" src="$(_layer PGF_BUILD "terminal: pgf use")"
  if [ "$sel" = auto ]; then
    key="${PGF_INFO_KEY[$variant]}"
    src="auto: this checkout's sources"
  elif prefix="$(python3 "$PGF_LIB/registry.py" resolve "$PGF_INFO_ROOT" "$variant" "$sel" 2>&1)"; then
    key="${prefix##*/}"
  else
    key="$sel" state="${prefix#*: }"
  fi
  if [ -z "${state:-}" ]; then
    if [ -z "$key" ]; then key="?" state="key unknown: cannot source OpenFOAM"
    elif [ -f "$PGF_INFO_ROOT/$key/.pgf-build.json" ]; then state="built"
    else state="not built: run pgf build"; [ "$variant" = yade1 ] && state+=" --yade"; fi
  fi
  printf 'settings  %s\n          %s\n' "${out[0]}" "${out[1]}"
  printf 'builds    %s [%s]\n' "${PGF_INFO_ROOT:-unknown}" "$(_layer PGF_BUILD_ROOT)"
  printf 'records   %s\n' "$(pgf_records_dir)"
  printf 'run dir   %s [%s]\n' "${PGF_RUN_ROOT:-${TMPDIR:-/tmp}/pgf-runs}" "$(_layer PGF_RUN_ROOT)"
  printf 'build     %s [%s; %s] %s\n' "$key" "$variant" "$src" "$state"
}

# pgf_build_info — set PGF_INFO_ROOT and PGF_INFO_KEY[yade0|yade1] for this
#   checkout (empty when OpenFOAM cannot be sourced). Needs SELF and PGF_ROOT.
pgf_build_info() {
  local out
  declare -gA PGF_INFO_KEY=()
  out="$("$SELF" exec --bare --no-yade -- /bin/bash -c \
    'printenv PGF_BUILD_ROOT; "$1" --print-key; "$1" --print-key --yade' _ "$PGF_LIB/build.sh" 2>/dev/null)"
  { read -r PGF_INFO_ROOT; read -r PGF_INFO_KEY[yade0]; read -r PGF_INFO_KEY[yade1]; } <<<"$out"
  [ -n "${PGF_INFO_ROOT:-}" ] || PGF_INFO_ROOT="${PGF_BUILD_ROOT:-}"
}

# pgf_ls_cmd [--builds-only] [--json] — registered workspaces and every build
#   under the build root (pgf ls; pgf builds is --builds-only); * marks this checkout's.
pgf_ls_cmd() {
  local json=() only=() k cur=()
  while [ $# -gt 0 ]; do
    case "$1" in
    --json) json=(--json); shift ;;
    --builds-only) only=(--builds-only); shift ;;
    *) echo "pgf: ls and builds take only --json" >&2; return 2 ;;
    esac
  done
  pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
  pgf_build_info
  [ -n "$PGF_INFO_ROOT" ] || { echo "pgf: no build root: set PGF_BUILD_ROOT or PGF_FOAM_BASHRC" >&2; return 2; }
  for k in "${PGF_INFO_KEY[@]}"; do [ -n "$k" ] && cur+=("$k"); done
  python3 "$PGF_LIB/registry.py" ls "$PGF_INFO_ROOT" "${json[@]}" "${only[@]}" --current "${cur[@]}"
}

# pgf_use_cmd SEL [--yade | --no-yade] — print the shell line that makes SEL
#   this terminal's build: eval "$(pgf use SEL)". pgf shell defines that eval.
pgf_use_cmd() {
  local sel="" variant prefix err
  pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
  variant="yade${PGF_YADE:-0}"
  while [ $# -gt 0 ]; do
    case "$1" in
    --yade) variant=yade1 ;;
    --no-yade) variant=yade0 ;;
    -h | --help) echo "usage: eval \"\$(pgf use <key|commit|worktree|prefix|auto> [--yade])\""; return 0 ;;
    -*) echo "pgf use: unknown option '$1'" >&2; return 2 ;;
    *) [ -z "$sel" ] || { echo "pgf use: one selector only" >&2; return 2; }; sel="$1" ;;
    esac
    shift
  done
  [ -n "$sel" ] || { echo "usage: eval \"\$(pgf use <key|commit|worktree|prefix|auto> [--yade])\"" >&2; return 2; }
  if [ "$sel" = auto ]; then
    echo "unset PGF_BUILD"
    echo "pgf: this terminal's build is auto again (the build matching the checkout's sources)" >&2
    return 0
  fi
  pgf_build_info
  prefix="$(python3 "$PGF_LIB/registry.py" resolve "$PGF_INFO_ROOT" "$variant" "$sel")" || return 2
  printf 'export PGF_BUILD=%q\n' "${prefix##*/}"
  echo "pgf: this terminal now uses ${prefix##*/}" >&2
  [ -t 1 ] && echo "pgf: printing only; apply it with: eval \"\$(pgf use $sel)\"" >&2
  return 0
}

# pgf_gc_cmd [--keep N] [--yes] [--dry-run] [--json] — remove builds no workspace
#   last built, none in use or building, beyond the N newest (default 2).
pgf_gc_cmd() {
  local args=() k cur=()
  while [ $# -gt 0 ]; do
    case "$1" in
    --keep)
      [[ "${2:-}" =~ ^[0-9]+$ ]] || { echo "pgf gc: --keep needs a number" >&2; return 2; }
      args+=(--keep "$2"); shift ;;
    --yes | --dry-run | --json) args+=("$1") ;;
    -h | --help) echo "usage: pgf gc [--keep N] [--yes] [--dry-run] [--json]"; return 0 ;;
    *) echo "pgf gc: unexpected '$1'" >&2; return 2 ;;
    esac
    shift
  done
  pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
  pgf_build_info
  [ -n "$PGF_INFO_ROOT" ] || { echo "pgf: no build root: set PGF_BUILD_ROOT or PGF_FOAM_BASHRC" >&2; return 2; }
  for k in "${PGF_INFO_KEY[@]}" "${PGF_BUILD:-}"; do [ -n "$k" ] && cur+=("$k"); done
  python3 "$PGF_LIB/registry.py" gc "$PGF_INFO_ROOT" "${args[@]}" --protect "${cur[@]}"
}

# pgf_status_json — settings, layers and this checkout's builds as JSON.
pgf_status_json() {
  pgf_build_info
  python3 "$PGF_LIB/registry.py" status "$(pgf_conf_json)" "$PGF_INFO_ROOT" "$PGF_ROOT" \
    "yade0=${PGF_INFO_KEY[yade0]}" "yade1=${PGF_INFO_KEY[yade1]}"
}

# pgf_status_builds — the human form of the builds part of status.
pgf_status_builds() {
  local v key state
  pgf_build_info
  echo "builds for $PGF_ROOT (root ${PGF_INFO_ROOT:-unknown}):"
  for v in yade0 yade1; do
    key="${PGF_INFO_KEY[$v]}"
    if [ -z "$key" ]; then state="key unknown (cannot source OpenFOAM)"
    elif [ -f "$PGF_INFO_ROOT/$key/.pgf-build.json" ]; then state="built"
    else state="not built"; fi
    printf '  %-6s %-44s %s\n' "$v" "${key:--}" "$state"
  done
}
