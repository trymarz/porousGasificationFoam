#!/bin/bash
# commands.sh — pgf's setup / config / build / doctor helpers (#257, #264).
# Source-only; needs config.sh, PGF_ROOT, SELF and PGF_ENV_CALLER_PATH.

# The OpenFOAM release this solver builds on; v2406 lives on maint/of2406.
PGF_FOAM_TARGET=v2606

# pgf_foam_fix VER — the fix line for an OpenFOAM other than PGF_FOAM_TARGET.
pgf_foam_fix() {
  printf 'fix: pgf config set PGF_FOAM_BASHRC=<OpenFOAM-%s>/etc/bashrc' "$PGF_FOAM_TARGET"
  [ "${1:-}" = v2406 ] && printf ', or use tag v2406-final / branch maint/of2406 for v2406'
  return 0
}

# pgf_case_wants_yade DIR — DIR/constant/yadeProperties says active true.
pgf_case_wants_yade() {
  grep -qE '^[[:space:]]*active[[:space:]]+(true|yes|on|1)[[:space:]]*;' \
    "$1/constant/yadeProperties" 2>/dev/null
}

# pgf_detect_bashrc — candidate OpenFOAM etc/bashrc files, PGF_FOAM_TARGET first.
pgf_detect_bashrc() {
  local c
  {
    [ -n "${WM_PROJECT_DIR:-}" ] && echo "$WM_PROJECT_DIR/etc/bashrc"
    for c in "$HOME"/OpenFOAM/*/etc/bashrc /opt/openfoam*/etc/bashrc \
      /usr/lib/openfoam/*/etc/bashrc; do
      echo "$c"
    done
  } | while IFS= read -r c; do [ -r "$c" ] && echo "$c"; done |
    awk '!seen[$0]++' | awk -v t="${PGF_FOAM_TARGET#v}" 'index($0, t) {print; next} {rest[++n]=$0} END {for (i=1;i<=n;i++) print rest[i]}'
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

# pgf_dangling_yade — yade links on the caller's PATH whose target is gone.
pgf_dangling_yade() {
  local d f
  local IFS=:
  for d in ${PGF_ENV_CALLER_PATH:-}; do
    for f in "$d"/yade "$d"/yade-* "$d"/yade[0-9]*; do
      [ -L "$f" ] && [ ! -e "$f" ] && echo "$f"
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

# pgf_init LAYER YES — merge CLI values (else detected ones) into the file,
#   printing one step line each for OpenFOAM, yade and the settings file.
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
    [ -d "$PGF_ROOT/submodules/yade-pgf/pkg/openfoam" ]; then
    out[YADE_TRUNK]="$PGF_ROOT/submodules/yade-pgf"
  fi
  for k in "${!PGF_CONF_CLI[@]}"; do out[$k]="${PGF_CONF_CLI[$k]}"; done
  pgf_setup_step_foam "${out[PGF_FOAM_BASHRC]:-}"
  pgf_setup_step_yade "${out[YADE_BIN]:-}" "${PGF_CONF_CLI[YADE_TRUNK]:-${YADE_TRUNK:-${out[YADE_TRUNK]:-$PGF_ROOT/submodules/yade-pgf}}}"

  # Remember old and new values for the outcome block; leave an unchanged file alone.
  PGF_SETUP_LAYER="$layer" PGF_SETUP_FILE="$file" PGF_SETUP_WROTE=0
  PGF_SETUP_OLD=() PGF_SETUP_NEW=()
  for k in "${!PGF_CONF_VAL[@]}"; do PGF_SETUP_OLD[$k]="${PGF_CONF_VAL[$k]}"; done
  for k in "${!out[@]}"; do PGF_SETUP_NEW[$k]="${out[$k]}"; done
  if [ -e "$file" ] && pgf_assoc_equal PGF_SETUP_OLD PGF_SETUP_NEW; then
    st_step "saving settings" unchanged "$(pgf_tilde "$file") ($layer layer)"
    return 0
  fi
  pgf_conf_write "$file" "$layer" out || { st_step "saving settings" FAIL "cannot write $(pgf_tilde "$file")"; return 1; }
  PGF_SETUP_WROTE=1
  st_step "saving settings" OK "wrote $(pgf_tilde "$file") ($layer layer)"
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

# pgf_setup_step_foam BASHRC — the "looking for OpenFOAM" step line.
pgf_setup_step_foam() {
  local ver
  if [ -z "$1" ] || [ ! -r "$1" ]; then
    st_step "looking for OpenFOAM" FAIL "$([ -n "$1" ] && echo "$(pgf_tilde "$1") is not readable" || echo "none found")"
    st_more "fix: ./pgf setup --foam-bashrc <OpenFOAM-$PGF_FOAM_TARGET>/etc/bashrc"
    return 0
  fi
  ver="$(pgf_foam_version "$1")"
  if [ "$ver" = "$PGF_FOAM_TARGET" ]; then
    st_step "looking for OpenFOAM" OK "$ver, $(pgf_tilde "$1")"
  else
    st_step "looking for OpenFOAM" WARN "${ver:-version unknown}, $(pgf_tilde "$1"); PGF targets $PGF_FOAM_TARGET"
    st_more "$(pgf_foam_fix "$ver")"
  fi
}

# pgf_setup_step_yade BIN TRUNK — the "looking for yade" step line; any pin
#   or trunk problem is a WARN that says only DEM runs need yade, as in pgf doctor.
pgf_setup_step_yade() {
  local line lvl msg dem="only DEM runs need it"
  if [ -z "$1" ]; then
    st_step "looking for yade" none "(only DEM runs need it; the CFD solver does not)"
    return 0
  fi
  if [ ! -x "$1" ]; then
    st_step "looking for yade" WARN "$(pgf_tilde "$1") is not executable; $dem"
    st_more "fix: pgf config set YADE_BIN=<yade binary>"
    return 0
  fi
  line="$(pgf_yade_check "$1" "$2" | head -1)"
  lvl="${line%% *}" msg="$(sed -E 's/^[A-Z]+ +//; s/^yade [^ ]+ //' <<<"$line")"
  pgf_coupling_api_hash "$2" >/dev/null || lvl=WARN
  if [ "$lvl" = OK ]; then
    st_step "looking for yade" OK "$(pgf_tilde "$1"), built from the pinned yade-pgf"
    return 0
  fi
  st_step "looking for yade" WARN "$(pgf_tilde "$1"); $dem"
  [ "${line%% *}" = OK ] || st_check WARN "$msg" | sed '1s/^  WARN /       /'
  pgf_coupling_api_hash "$2" >/dev/null ||
    st_check WARN "no yade-pgf sources in $(pgf_tilde "$2"); fix: $(pgf_trunk_fix "$2")" | sed '1s/^  WARN /       /'
}

# pgf_trunk_fix TRUNK — how to get yade-pgf sources into TRUNK.
pgf_trunk_fix() {
  if [ "$(cd "$1" 2>/dev/null && pwd -P)" = "$(cd "$PGF_ROOT/submodules/yade-pgf" 2>/dev/null && pwd -P)" ]; then
    echo "git submodule update --init submodules/yade-pgf"
  else
    echo "git submodule update --init submodules/yade-pgf, or set YADE_TRUNK"
  fi
}

# pgf_setup_step_link MODE — the "linking pgf" step line (MODE 0: --no-link);
#   sets PGF_SETUP_PGF to how the next steps should call pgf.
pgf_setup_step_link() {
  local p link="$HOME/.local/bin/pgf"
  PGF_SETUP_PGF=pgf
  p="$(PATH="$PGF_ENV_CALLER_PATH" command -v pgf)"
  if [ -n "$p" ] && [ "$(readlink -f "$p")" = "$SELF" ]; then
    st_step "linking pgf" OK "$(pgf_tilde "$link") → $(pgf_tilde "$SELF"), on PATH"
  elif [ -n "$p" ]; then
    PGF_SETUP_PGF=./pgf
    st_step "linking pgf" WARN "pgf on PATH is $(pgf_tilde "$(readlink -f "$p")"), not $(pgf_tilde "$SELF"); here call ./pgf"
    st_more "fix: ./pgf setup --link-only --force"
  elif [ "$(readlink -f "$link")" = "$SELF" ]; then
    st_step "linking pgf" WARN "$(pgf_tilde "$link") is set, but ~/.local/bin is not on your PATH"
    st_more 'fix: export PATH="$HOME/.local/bin:$PATH"   (add it to ~/.bashrc)'
  elif [ "$1" -eq 0 ]; then
    PGF_SETUP_PGF="$(pgf_tilde "$SELF")"
    st_step "linking pgf" skipped "(--no-link); call $(pgf_tilde "$SELF") by path"
  else
    PGF_SETUP_PGF=./pgf
    st_step "linking pgf" WARN "$(pgf_tilde "$link") points at $(pgf_tilde "$(readlink -f "$link")"), not here"
    st_more "fix: ./pgf setup --link-only --force"
  fi
}

# pgf_setup_outcome — what was saved where, then next steps.
pgf_setup_outcome() {
  local k new note w=0 pgf="${PGF_SETUP_PGF:-pgf}"
  echo
  if [ "$PGF_SETUP_WROTE" -eq 1 ]; then
    st_head "saved in $(pgf_tilde "$PGF_SETUP_FILE") ($PGF_SETUP_LAYER layer):"
  else
    st_head "kept in $(pgf_tilde "$PGF_SETUP_FILE") ($PGF_SETUP_LAYER layer), unchanged:"
  fi
  for k in "${!PGF_SETUP_NEW[@]}"; do
    new="$(pgf_tilde "${PGF_SETUP_NEW[$k]}")"
    [ ${#new} -gt "$w" ] && w=${#new}
  done
  for k in "${PGF_CONF_KEYS[@]}"; do
    [ -n "${PGF_SETUP_NEW[$k]+x}" ] || continue
    new="${PGF_SETUP_NEW[$k]}"
    if [ -z "${PGF_SETUP_OLD[$k]+x}" ]; then note="new"
    elif [ "${PGF_SETUP_OLD[$k]}" = "$new" ]; then note="kept"
    else note="was $(pgf_tilde "${PGF_SETUP_OLD[$k]}")"; fi
    printf "  %-16s %-${w}s  %s\n" "$k" "$(pgf_tilde "$new")" "$(pgf_ui_paint 2 "($note)")"
  done
  echo
  st_next "$pgf build" "build the solver of $(pgf_checkout_name) (≈1.5 min first build)" \
    "$pgf doctor" "check everything, with a fix for each problem" \
    "$pgf help config" "what each setting means; change one: pgf config set KEY=VALUE"
}

# pgf_self_cmd — how to call this pgf from here: pgf when it is the one on PATH,
#   else its path.
pgf_self_cmd() {
  local p
  p="$(PATH="${PGF_ENV_CALLER_PATH:-$PATH}" command -v pgf)"
  if [ -n "$p" ] && [ "$(readlink -f "$p")" = "$SELF" ]; then echo pgf; else pgf_tilde "$SELF"; fi
}

# pgf_stale_refusal SRC PREFIX AUTO FLAGS — why pgf run refuses PREFIX, and the
#   fix that fits what chose it (SRC: cli, terminal or a checkout file).
pgf_stale_refusal() {
  local src="$1" key="${2##*/}" auto="${3##*/}" flag="${4:+ $4}" p fix build="" name
  p="$(pgf_self_cmd)" name="$(pgf_checkout_name)"
  [ -f "$3/.pgf-build.json" ] || build=", then $p build$flag"
  case "$src" in
  cli) fix="leave out --use to run with the build of $name$build" ;;
  terminal) fix="eval \"\$($p use auto)\" to go back to the build of $name$build" ;;
  checkout*) fix="$p config unset PGF_BUILD --checkout$build" ;;
  *) fix="unset PGF_BUILD$build" ;;
  esac
  case "$src" in cli) src="chosen with --use" ;; terminal) src="chosen with pgf use" ;; checkout*) src="PGF_BUILD in $(pgf_tilde "${src#* }")" ;; esac
  { st FAIL "build ${key%%-*} ($src) is not built from the current sources of $name (${auto%%-*})"
    st_more "pgf run refuses a build of other sources; --allow-stale runs it anyway"
    st_more "fix: $fix"; } >&2
}

# pgf_case_logs — "name mtime" per log.* in the case dir; a run that changes
#   none of them ran nothing (runApplication skips a step whose log exists).
pgf_case_logs() {
  find . -maxdepth 1 -name 'log.*' -type f -printf '%f %T@\n' 2>/dev/null | LC_ALL=C sort
}

# pgf_run_precheck SCRIPT — before pgf run: warn when the case sits inside this
#   checkout or already holds output (log.*, processor*, time dirs) it did not write.
pgf_run_precheck() {
  local script="${1##*/}" here root old
  [ "$script" = Allclean ] && return 0
  here="$(pwd -P)" root="$(cd "$PGF_ROOT" && pwd -P)"
  case "$here/" in "$root"/*)
    { st WARN "$(pgf_tilde "$here") is inside the checkout $(pgf_tilde "$root"); results go into git-ignored files there,"
      st_more "and a later copy of the case takes them along. Run on a copy: pgf copy ${here#"$root"/tutorials/cases/}"; } >&2
    ;;
  esac
  old="$( { ls -d log.* processor[0-9]* 2>/dev/null
    ls -d [0-9]* 2>/dev/null | grep -E '^[0-9.e+-]+$' | grep -vx '0'; } | head -6 | tr '\n' ' ')"
  [ -n "$old" ] || return 0
  { st WARN "the case already holds output from an earlier run: ${old% }"
    st_more "runApplication skips every step whose log.* exists; clean first: pgf run $(pgf_tilde "$here") Allclean"; } >&2
}

# pgf_run_summary RC T0 SCRIPT LOGS_BEFORE — after pgf run in the case dir: OK,
#   FAILED or ran nothing; the time, the newest log this run wrote, next step.
pgf_run_summary() {
  local rc="$1" t0="$2" script="${3##*/}" before="$4" dt log last="" d changed
  dt=$(($(date +%s) - t0))
  dt="$([ "$dt" -ge 60 ] && printf '%dm%02ds' $((dt / 60)) $((dt % 60)) || printf '%ds' "$dt")"
  changed="$(LC_ALL=C comm -13 <(printf '%s\n' "$before") <(pgf_case_logs) | sort -k2 -g | tail -1)"
  log="${changed% *}"
  for d in . processor0; do
    [ -d "$d" ] && last="$(cd "$d" && ls -d [0-9]* 2>/dev/null | grep -E '^[0-9.e+-]+$' | sort -g | tail -1)"
    [ -n "$last" ] && [ "$last" != 0 ] && break
  done
  echo >&2
  {
    if [ "$rc" -eq 0 ] && [ -z "$log" ] && [ -n "$before" ] && [ "$script" != Allclean ]; then
      st WARN "$script ran nothing (exit 0 after $dt, $(pgf_tilde "$PWD"))"
      st_more "no log.* written by this run: every step was skipped because its log.* already existed,"
      st_more "so the results here are from an earlier run."
      st_more "record: .pgf-run.json"
      st_next "pgf run $(pgf_tilde "$PWD") Allclean" "clean first, then run again"
      return 0
    elif [ "$rc" -eq 0 ]; then
      st OK "$script in $dt ($(pgf_tilde "$PWD"))"
      [ -z "$last" ] || [ "$last" = 0 ] || st_more "latest time: $last"
      [ -z "$log" ] || st_more "last log:    $log"
    else
      st FAIL "$script (exit $rc$([ "$rc" -eq 130 ] && echo ', interrupted')) after $dt ($(pgf_tilde "$PWD"))"
      if [ -n "$log" ]; then
        local fatal
        fatal="$(grep -n -m1 'FOAM FATAL' "$log" | cut -d: -f1)"
        if [ -n "$fatal" ]; then
          st_more "$log from its FOAM FATAL message:"
          tail -n "+$((fatal > 2 ? fatal - 2 : 1))" "$log" | head -n 25 | sed 's/^/    /'
        else
          st_more "last 20 lines of $log:"
          tail -n 20 "$log" | sed 's/^/    /'
        fi
      else
        st_more "no log.* written by this run; the script's own output is above"
      fi
    fi
    st_more "record:      .pgf-run.json"
    [ "$rc" -eq 0 ] && [ -n "$last" ] && [ "$last" != 0 ] &&
      st_next "pgf view $(pgf_tilde "$PWD")" "look at the results in ParaView (doc/paraview.md)"
  } >&2
  return 0
}

# pgf_install_link FORCE — ~/.local/bin/pgf -> this checkout's pgf.
pgf_install_link() {
  local link="$HOME/.local/bin/pgf" cur
  if [ -L "$link" ] || [ -e "$link" ]; then
    cur="$(readlink -f "$link")"
    if [ "$cur" = "$SELF" ]; then
      [ "${PGF_SETUP_QUIET:-0}" -eq 1 ] || echo "pgf: $link already points here" >&2
      return 0
    fi
    [ "$1" -eq 1 ] || {
      [ "${PGF_SETUP_QUIET:-0}" -eq 1 ] && return 2 # the setup summary warns
      echo "pgf: $link points at ${cur:-a file}; rerun with --force to replace it" >&2
      echo "pgf: (with several worktrees, the link picks one; call the others by path)" >&2
      return 2
    }
  fi
  mkdir -p "${link%/*}" && ln -sfn "$SELF" "$link" || return 2
  # The setup summary reports the link and PATH; --link-only has no summary.
  [ "${PGF_SETUP_QUIET:-0}" -eq 1 ] && return 0
  echo "pgf: $(pgf_tilde "$link") -> $(pgf_tilde "$SELF")" >&2
  case ":$PGF_ENV_CALLER_PATH:" in
  *":${link%/*}:"*) ;;
  *) echo "pgf: note: ${link%/*} is not on your PATH" >&2 ;;
  esac
}

# pgf_doctor_trunk_source TRUNK — warn (via _r) when YADE_TRUNK comes from the
#   environment, and when the checkout's own submodule is off its pin.
pgf_doctor_trunk_source() {
  local own="$PGF_ROOT/submodules/yade-pgf" line
  if [ "${PGF_CONF_SRC[YADE_TRUNK]:-}" = env ]; then
    _r WARN "YADE_TRUNK=$1 comes from the environment and overrides the checkout's submodule; find the export: grep -n YADE_TRUNK ~/.bashrc ~/.profile, or unset it"
  fi
  [ "$(cd "$1" 2>/dev/null && pwd -P)" = "$(cd "$own" 2>/dev/null && pwd -P)" ] && return 0
  [ -e "$own/.git" ] || return 0
  while IFS= read -r line; do _r "${line%% *}" "${line#* }"; done < <(pgf_trunk_pin_check "$own")
}

# pgf_paraview_check — one OK, WARN or SKIP line on the ParaView on PATH, for pgf view:
#   paraview.simple must import in pvpython. Names Debian's python3-vtk9 clash.
pgf_paraview_check() {
  local pv ver
  pv="$(command -v pvpython)"
  if [ -z "$pv" ]; then
    if command -v paraview >/dev/null; then
      echo "WARN ParaView $(pgf_tilde "$(command -v paraview)") has no Python (no pvpython); fix: sudo apt install python3-paraview (doc/install.md, ParaView and Yade's VTK)"
    else
      echo "SKIP no ParaView on PATH; only pgf view needs it (doc/paraview.md)"
    fi
    return 0
  fi
  ver="$(UCX_VFS_ENABLE=n timeout 60 "$pv" -c 'import paraview.simple as s; v = s.GetParaViewVersion(); print("%s.%s" % (v.major, v.minor))' 2>/dev/null | tail -1)"
  if [ -n "$ver" ]; then
    echo "OK ParaView $ver with Python, $(pgf_tilde "$pv")"
  else
    echo "WARN $(pgf_tilde "$pv") cannot import paraview.simple; fix: install a ParaView with Python (doc/paraview.md, Requirements)"
  fi
}

# pgf_doctor — read-only checks in groups, one line each with the fix under a
#   WARN or FAIL, then a verdict; returns 1 when any FAIL. OpenFOAM is sourced
#   in children only.
pgf_doctor() {
  local fails=0 need_build=0 dem_ok=1 grp="" bashrc ver yb out trunk why p f v key line libdir
  local -a dem=()
  _r() {
    st_check "$1" "$2"
    [ "$1" = FAIL ] && fails=$((fails + 1))
    [ "$grp" = dem ] && [ "$1" != OK ] && dem_ok=0
    return 0
  }
  _dem() { [[ " ${dem[*]} " == *" $1 "* ]] || dem+=("$1"); }
  echo "pgf doctor: read-only checks that pgf can build and run cases from $(pgf_tilde "$PGF_ROOT")"

  echo
  st_head "OpenFOAM"
  bashrc="${PGF_FOAM_BASHRC:-${WM_PROJECT_DIR:+$WM_PROJECT_DIR/etc/bashrc}}"
  if [ -n "$bashrc" ] && [ -r "$bashrc" ]; then
    ver="$(pgf_foam_version "$bashrc")"
    if [ -z "$ver" ]; then
      _r FAIL "$(pgf_tilde "$bashrc") sets no WM_PROJECT_VERSION; $(pgf_foam_fix)"
    elif [ "$ver" != "$PGF_FOAM_TARGET" ]; then
      _r WARN "$ver, $(pgf_tilde "$bashrc"); the solver targets $PGF_FOAM_TARGET; $(pgf_foam_fix "$ver")"
    else
      _r OK "$ver, $(pgf_tilde "$bashrc")"
    fi
  else
    _r FAIL "no readable OpenFOAM bashrc (${bashrc:-none set}); fix: ./pgf setup, or pgf config set PGF_FOAM_BASHRC=FILE"
  fi

  yb="${YADE_BIN:-$(PATH="$PGF_ENV_CALLER_PATH" command -v yade)}"
  if [ "$fails" -eq 0 ]; then
    echo
    st_head "build"
    pgf_build_info
    for v in yade0 yade1; do
      key="${PGF_INFO_KEY[$v]}"
      [ "$v" = yade1 ] && [ -z "$yb" ] && [ "${PGF_YADE:-0}" != 1 ] && continue
      [ "$v" = yade1 ] && [ "${PGF_YADE:-0}" != 1 ] && grp=dem
      local what="non-DEM" flag=""
      [ "$v" = yade1 ] && what="DEM" flag=" --yade"
      if [ -n "$key" ] && [ -f "$PGF_INFO_ROOT/$key/.pgf-build.json" ]; then
        _r OK "$what build ${key%%-*} of the sources of $(pgf_checkout_name)"
      else
        _r WARN "no $what build of the sources of $(pgf_checkout_name) yet; fix: pgf build$flag"
        if [ "$grp" = dem ]; then _dem "a DEM build (pgf build --yade)"; else need_build=1; fi
      fi
      grp=""
    done
  fi

  echo
  st_head "yade and DEM"
  while IFS= read -r f; do
    st_check WARN "$(pgf_tilde "$f") on PATH is a dangling link to $(readlink "$f"); fix: remove it, or point it at a yade"
  done < <(pgf_dangling_yade)
  [ "${PGF_YADE:-0}" = 1 ] || grp=dem
  if [ -z "$yb" ] && [ "${PGF_YADE:-0}" != 1 ]; then
    st SKIP "no yade configured; only DEM runs need it"
    _dem "yade (pgf config set YADE_BIN=<yade>)"
    dem_ok=0
  elif [ ! -x "$yb" ]; then
    _r "$([ "${PGF_YADE:-0}" = 1 ] && echo FAIL || echo WARN)" "no yade binary at ${yb:-none}; fix: pgf config set YADE_BIN=<yade> (doc/install.md, 3. DEM coupling)"
    _dem "yade (pgf config set YADE_BIN=<yade>)"
  else
    out="$(timeout 60 "$yb" -v 2>/dev/null | grep -m1 -i 'version' | sed 's/^ *//')"
    if timeout 60 "$yb" --help 2>/dev/null | grep -q 'MPI'; then
      _r OK "yade $(pgf_tilde "$yb")${out:+ ($out)}, with MPI"
    else
      _r WARN "yade $(pgf_tilde "$yb") does not list MPI among its features; the coupling needs a yade built with ENABLE_MPI"
      _dem "a yade built with MPI"
    fi
    while IFS= read -r line; do
      [ "${line%% *}" = OK ] || _dem "a yade built from the yade-pgf pin"
      _r "$(sed 's/^FAIL/WARN/; s/ .*//' <<<"$line")" "$(sed -E 's/^[A-Z]+ +//' <<<"$line")"
    done < <(pgf_yade_check "$yb" "${YADE_TRUNK:-$PGF_ROOT/submodules/yade-pgf}")
  fi
  trunk="${YADE_TRUNK:-$PGF_ROOT/submodules/yade-pgf}"
  if [ -n "$yb" ] || [ "${PGF_YADE:-0}" = 1 ]; then
    if pgf_coupling_api_hash "$trunk" >/dev/null; then
      _r OK "PgfCoupling sources in $(pgf_tilde "$trunk")"
      while IFS= read -r line; do _r "${line%% *}" "${line#* }"; done < <(pgf_trunk_pin_check "$trunk")
      pgf_doctor_trunk_source "$trunk"
    else
      _r "$([ "${PGF_YADE:-0}" = 1 ] && echo FAIL || echo WARN)" "no PgfCoupling sources under $(pgf_tilde "$trunk"); fix: git submodule update --init submodules/yade-pgf && git -C submodules/yade-pgf submodule update --init extra/PgfDemBridge, or set YADE_TRUNK"
      _dem "the yade-pgf submodule"
    fi
  fi
  grp=""

  echo
  st_head "MPI"
  case "${PGF_MPI:-auto}" in
  auto)
    if why="$(pgf_mpi_auto)"; then _r OK "auto: local only (lo), because $why"; else _r OK "auto: Open MPI left alone ($why)"; fi
    ;;
  *) _r OK "PGF_MPI=${PGF_MPI}, set by you; interfaces: $(pgf_mpi_ifaces | tr '\n' ' ' | sed 's/ $//')" ;;
  esac

  echo
  st_head "ParaView"
  line="$(PATH="$PGF_ENV_CALLER_PATH" pgf_paraview_check)"
  if [ "${line%% *}" = SKIP ]; then st SKIP "${line#* }"; else _r "${line%% *}" "${line#* }"; fi

  echo
  st_head "PATH"
  p="$(PATH="$PGF_ENV_CALLER_PATH" command -v pgf)"
  if [ -z "$p" ]; then
    _r WARN "pgf is not on PATH; fix: $(pgf_tilde "$SELF") setup --link-only"
  elif [ "$(readlink -f "$p")" != "$SELF" ]; then
    _r WARN "pgf on PATH is $(pgf_tilde "$(readlink -f "$p")"), not $(pgf_tilde "$SELF"); call the latter by path, or: $(pgf_tilde "$SELF") setup --link-only --force"
  else
    _r OK "pgf on PATH is $(pgf_tilde "$SELF")"
  fi
  f="$(pgf_completion_file)"
  if [ -f "$f" ] && ! cmp -s "$f" <(pgf_completion_cmd bash); then
    _r WARN "the bash completion in $(pgf_tilde "$f") is from another pgf version; fix: pgf completion bash > $(pgf_tilde "$f")"
  fi

  echo
  if [ "$fails" -gt 0 ]; then
    v="not ready: fix the FAIL lines above"
  elif [ "$need_build" -eq 1 ]; then
    v="set up; build before running: pgf build"
  else
    v="ready for non-DEM runs"
  fi
  if [ "$fails" -eq 0 ]; then
    if [ "${#dem[@]}" -gt 0 ]; then
      local IFS=';'
      v+="; DEM needs:$(printf ' %s;' "${dem[@]}")"
      v="${v%;}"
    elif [ "$dem_ok" -eq 1 ]; then
      v+=", DEM runs included"
      [ "$need_build" -eq 1 ] || v="ready, DEM runs included"
    fi
  fi
  printf '%s %s\n' "$(pgf_ui_paint 1 "verdict:")" "$v"
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
    st OK "$verb $k in $(pgf_tilde "$file")" >&2
    ;;
  esac
}

# pgf_build_cmd [--yade] [pgf env options] [build.sh options] — build this tree
#   into the build store (build.sh --isolated); an identical build is reused.
pgf_build_cmd() {
  local envopts=() bopts=() build=1
  while [ $# -gt 0 ]; do
    case "$1" in
    --yade) envopts+=(--yade); bopts+=(--yade) ;;
    --set | --foam-bashrc | --mpi) envopts+=("$1" "${2:-}"); shift ;;
    clean | --dry-run | --print-* | --fingerprint) bopts+=("$1"); build=0 ;;
    *) bopts+=("$1") ;;
    esac
    shift
  done
  PGF_BANNER_BUILD=1 "$SELF" shell --bare "${envopts[@]}" -- "$PGF_LIB/build.sh" --isolated "${bopts[@]}" || return $?
  [ "$build" -eq 0 ] || [ -n "${PGF_IMAGE_MANIFEST:-}" ] || # pgf pack's image build
    st_next "pgf copy --list" "the tutorials; pgf copy <tutorial> makes a copy to run" \
      "pgf test fast" "the fast test tier" >&2
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

# pgf_layer KEY [ENV-NAME] — the layer that set KEY, in words.
pgf_layer() {
  case "${PGF_CONF_SRC[$1]:-default}" in
  env) echo "${2:-environment}" ;;
  cli) echo "command line" ;;
  default) echo default ;;
  *) local s="${PGF_CONF_SRC[$1]}"; echo "${s%% *} $(pgf_tilde "${s#* }")" ;;
  esac
}

# pgf_checkout_desc DIR — "branch NAME @ abc1234" or "detached @ abc1234".
pgf_checkout_desc() {
  local b c
  c="$(git -C "$1" rev-parse --short HEAD 2>/dev/null)" || { echo "not a git checkout"; return; }
  b="$(git -C "$1" symbolic-ref --short -q HEAD)"
  if [ -n "$b" ]; then echo "branch $b @ $c"; else echo "detached @ $c"; fi
}

# pgf_status — what pgf uses and where, on one screen: checkout, files, the
#   active build and what chose it, this checkout's builds, case copies.
pgf_status() {
  local f files=() variant sel src key prefix state v w yb line
  pgf_build_info
  for f in "$(pgf_conf_user_file)" "$(pgf_conf_checkout_read_file)"; do
    if [ -e "$f" ]; then files+=("$(pgf_tilde "$f")"); else files+=("$(pgf_tilde "$f") (absent)"); fi
  done
  variant="yade${PGF_YADE:-0}" sel="${PGF_BUILD:-auto}" src="$(pgf_layer PGF_BUILD "terminal: pgf use")"
  if [ "$sel" = auto ]; then
    key="${PGF_INFO_KEY[$variant]}"
    src="auto: the sources of $(pgf_checkout_name)"
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
  printf 'checkout  %s (%s)\n' "$(pgf_tilde "$PGF_ROOT")" "$(pgf_checkout_desc "$PGF_ROOT")"
  printf 'settings  %s\n          %s\n' "${files[0]}" "${files[1]}"
  printf 'builds    %s [%s]\n' "$(pgf_tilde "${PGF_INFO_ROOT:-unknown}")" "$(pgf_layer PGF_BUILD_ROOT)"
  printf 'records   %s\n' "$(pgf_tilde "$(pgf_records_dir)")"
  printf 'scratch   %s [%s]\n' "$(pgf_tilde "${PGF_RUN_ROOT:-${TMPDIR:-/tmp}/pgf-runs}")" "$(pgf_layer PGF_RUN_ROOT)"
  printf 'cases     %s [%s]\n' "$(pgf_tilde "${PGF_INFO_CASES:-unknown}")" \
    "$([ -n "${PGF_CONF_SRC[PGF_CASE_ROOT]:-}" ] && pgf_layer PGF_CASE_ROOT || echo 'default: $WM_PROJECT_USER_DIR/run')"
  printf 'build     %s [%s; %s] %s\n' "$key" "$variant" "$src" "$state"
  echo
  st_head "builds of $(pgf_checkout_name):"
  for v in yade0 yade1; do
    key="${PGF_INFO_KEY[$v]}" w="non-DEM"
    [ "$v" = yade1 ] && w="DEM"
    if [ -z "$key" ]; then state="key unknown (cannot source OpenFOAM)"
    elif [ -f "$PGF_INFO_ROOT/$key/.pgf-build.json" ]; then state="built"
    else state="not built: pgf build$([ "$v" = yade1 ] && echo ' --yade')"; fi
    printf '  %-7s %-6s %-44s %s\n' "$w" "$v" "${key:--}" "$state"
  done
  yb="${YADE_BIN:-$(PATH="$PGF_ENV_CALLER_PATH" command -v yade)}"
  if [ -x "$yb" ]; then
    echo
    st_head "yade for DEM runs:"
    while IFS= read -r line; do
      st_check "$(sed 's/^FAIL/WARN/; s/ .*//' <<<"$line")" "$(sed -E 's/^[A-Z]+ +//' <<<"$line")"
    done < <(pgf_yade_check "$yb" "${YADE_TRUNK:-$PGF_ROOT/submodules/yade-pgf}")
  fi
  echo
  st_head "case copies (pgf copy), per branch:"
  out="$(python3 "$PGF_LIB/copies.py" list "${PGF_INFO_CASES:-/nonexistent}")"
  if [ -n "$out" ]; then printf '%s\n' "$out"; else echo "  none yet; see pgf copy --list"; fi
  echo
  echo "Every setting and its layer: pgf config. Checks with fixes: pgf doctor."
}

# pgf_build_info — set PGF_INFO_ROOT, PGF_INFO_CASES and PGF_INFO_KEY[yade0|yade1]
#   for this checkout (empty when OpenFOAM cannot be sourced). Needs SELF, PGF_ROOT.
pgf_build_info() {
  local out
  declare -gA PGF_INFO_KEY=()
  out="$("$SELF" shell --bare --no-yade -- /bin/bash -c \
    'printenv PGF_BUILD_ROOT; printenv PGF_CASE_ROOT; "$1" --print-key; "$1" --print-key --yade' _ "$PGF_LIB/build.sh" 2>/dev/null)"
  { read -r PGF_INFO_ROOT; read -r PGF_INFO_CASES; read -r PGF_INFO_KEY[yade0]; read -r PGF_INFO_KEY[yade1]; } <<<"$out"
  [ -n "${PGF_INFO_ROOT:-}" ] || PGF_INFO_ROOT="${PGF_BUILD_ROOT:-}"
  [ -n "${PGF_INFO_CASES:-}" ] || PGF_INFO_CASES="${PGF_CASE_ROOT:-}"
}

# pgf_ls_cmd [--json] — registered workspaces and every build under the build
#   root (pgf builds; old name pgf ls); * marks the builds of the checkout pgf runs from.
pgf_ls_cmd() {
  local json=() k cur=()
  while [ $# -gt 0 ]; do
    case "$1" in
    --json) json=(--json); shift ;;
    *) echo "pgf builds: unexpected '$1' (it takes only --json)" >&2; return 2 ;;
    esac
  done
  pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }
  pgf_build_info
  [ -n "$PGF_INFO_ROOT" ] || { echo "pgf: no build root: set PGF_BUILD_ROOT or PGF_FOAM_BASHRC" >&2; return 2; }
  for k in "${PGF_INFO_KEY[@]}"; do [ -n "$k" ] && cur+=("$k"); done
  python3 "$PGF_LIB/registry.py" ls "$PGF_INFO_ROOT" "${json[@]}" --name "$(pgf_checkout_name)" --current "${cur[@]}"
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
  # On a terminal nobody evals the line, so nothing changes.
  if [ "$sel" = auto ]; then
    echo "unset PGF_BUILD"
    if [ -t 1 ]; then echo "pgf: printing only; to go back to the build of $(pgf_checkout_name)'s sources: eval \"\$($(pgf_self_cmd) use auto)\"" >&2
    else echo "pgf: this terminal's build is auto again (the build of $(pgf_checkout_name)'s sources)" >&2; fi
    return 0
  fi
  pgf_build_info
  prefix="$(python3 "$PGF_LIB/registry.py" resolve "$PGF_INFO_ROOT" "$variant" "$sel")" || return 2
  printf 'export PGF_BUILD=%q\n' "${prefix##*/}"
  if [ -t 1 ]; then echo "pgf: printing only; to use ${prefix##*/} in this terminal: eval \"\$($(pgf_self_cmd) use $sel)\"" >&2
  else echo "pgf: this terminal now uses ${prefix##*/}" >&2; fi
  [ "${prefix##*/}" = "${PGF_INFO_KEY[$variant]:-}" ] ||
    echo "pgf: note: ${prefix##*/} is not built from the current sources of $(pgf_checkout_name); pgf run refuses it unless --allow-stale" >&2
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
  for k in "${PGF_INFO_KEY[@]}"; do [ -n "$k" ] && cur+=("$k"); done
  [ -z "${PGF_BUILD:-}" ] || args+=(--selected "$PGF_BUILD")
  python3 "$PGF_LIB/registry.py" gc "$PGF_INFO_ROOT" "${args[@]}" --name "$(pgf_checkout_name)" --protect "${cur[@]}"
}

# pgf_status_json — settings, layers and this checkout's builds as JSON.
pgf_status_json() {
  pgf_build_info
  python3 "$PGF_LIB/registry.py" status "$(pgf_conf_json)" "$PGF_INFO_ROOT" "$PGF_ROOT" \
    "yade0=${PGF_INFO_KEY[yade0]}" "yade1=${PGF_INFO_KEY[yade1]}"
}

# pgf_tutorials — the tutorial cases, one path per line (for completion).
pgf_tutorials() {
  python3 "$PGF_LIB/copies.py" tutorials "$PGF_ROOT" | cut -f1
}

# pgf_branch_dir — this checkout's branch with / as -, or its short commit
#   when detached; "checkout" outside git.
pgf_branch_dir() {
  local b
  b="$(git -C "$PGF_ROOT" symbolic-ref --short -q HEAD 2>/dev/null)" ||
    b="$(git -C "$PGF_ROOT" rev-parse --short HEAD 2>/dev/null)" || b=checkout
  printf '%s' "${b//\//-}"
}

# pgf_copy_outside DIR — the first non-comment Allrun/Allclean line in DIR that
#   reaches a bare ../ path (not through ${PGF_ROOT:-…}); a copy can't run it.
pgf_copy_outside() {
  grep -HnE '(^|[[:space:]"'\''(=])\.\./' "$1/Allrun" "$1/Allclean" 2>/dev/null |
    grep -vE '^[^:]*:[0-9]+:[[:space:]]*#' | grep -v 'PGF_ROOT:-' | head -1
}

# pgf_copy_cmd [--list] [--force] TUTORIAL [NAME] — copy a tutorial case to
#   $PGF_CASE_ROOT/<branch>/<TUTORIAL or NAME> and record where it came from.
pgf_copy_cmd() {
  local list=0 force=0 pos=() root src rel dest out
  local usage="usage: pgf copy <tutorial> [NAME] [--force] | pgf copy --list"
  while [ $# -gt 0 ]; do
    case "$1" in
    --list) list=1 ;;
    --force) force=1 ;;
    -*) echo "pgf copy: unknown option '$1'; $usage" >&2; return 2 ;;
    *) pos+=("$1") ;;
    esac
    shift
  done
  if [ "$list" -eq 1 ]; then
    [ ${#pos[@]} -eq 0 ] || { echo "$usage" >&2; return 2; }
    echo "tutorials in $(pgf_tilde "$PGF_ROOT")/tutorials/cases (pgf copy <tutorial>):"
    python3 "$PGF_LIB/copies.py" tutorials "$PGF_ROOT" | awk -F'\t' '
      { p[NR] = $1; d[NR] = $2; if (length($1) > w) w = length($1) }
      END { for (i = 1; i <= NR; i++) { s = sprintf("  %-" w "s  %s", p[i], d[i]); sub(/ +$/, "", s); print s } }'
    return 0
  fi
  [ ${#pos[@]} -ge 1 ] && [ ${#pos[@]} -le 2 ] || { echo "$usage" >&2; return 2; }
  pgf_conf_resolve || { echo "pgf: $PGF_CONF_ERR" >&2; return 2; }

  rel="${pos[0]%/}" rel="${rel#tutorials/cases/}"
  src="$PGF_ROOT/tutorials/cases/$rel"
  if [ ! -d "$src" ] && [ -d "${pos[0]}" ]; then
    src="$(cd "${pos[0]}" && pwd -P)"
    case "$src/" in "$(cd "$PGF_ROOT" && pwd -P)/tutorials/cases/"*) rel="${src#"$(cd "$PGF_ROOT" && pwd -P)"/tutorials/cases/}" ;; *) rel="${src##*/}" ;; esac
  fi
  [ -d "$src" ] || { echo "pgf copy: no tutorial '${pos[0]}'; see pgf copy --list" >&2; return 2; }
  [ -x "$src/Allrun" ] || { echo "pgf copy: $(pgf_tilde "$src") has no Allrun; see pgf copy --list" >&2; return 2; }
  out="$(pgf_copy_outside "$src")"
  if [ -n "$out" ]; then
    st FAIL "$rel can't run from a copy: its scripts reach outside the case by a bare ../" >&2
    st_more "${out#"$src"/}" >&2
    st_more "call such helpers as \"\${PGF_ROOT:-../..}/tutorials/cases/…\" instead (#290)" >&2
    return 2
  fi

  root="${PGF_CASE_ROOT:-}"
  [ -n "$root" ] || root="$("$SELF" shell --bare --no-yade -- printenv PGF_CASE_ROOT 2>/dev/null)"
  [ -n "$root" ] || { echo "pgf copy: no case root: run ./pgf setup (OpenFOAM's \$WM_PROJECT_USER_DIR/run is the default), or pgf config set PGF_CASE_ROOT=DIR" >&2; return 2; }
  dest="$root/$(pgf_branch_dir)/${pos[1]:-$rel}"
  if [ -e "$dest" ]; then
    if [ "$force" -eq 0 ]; then
      st FAIL "$(pgf_tilde "$dest") already exists; kept it" >&2
      st_more "run it: pgf run $(pgf_tilde "$dest"); or replace it: pgf copy ${pos[*]} --force" >&2
      return 2
    fi
    rm -rf -- "$dest" || { echo "pgf copy: cannot remove $dest" >&2; return 2; }
  fi
  mkdir -p -- "${dest%/*}" && cp -rL -- "$src" "$dest" || { echo "pgf copy: cannot copy to $dest" >&2; return 2; }
  python3 "$PGF_LIB/copies.py" record "$dest" "$PGF_ROOT" "$rel" ||
    st WARN "could not record the source in $(pgf_tilde "$dest")/.pgf-source.json" >&2
  st OK "copied $rel from $(pgf_checkout_desc "$PGF_ROOT")"
  st_more "to $(pgf_tilde "$dest")"
  out="$( { ls -d "$dest"/log.* "$dest"/processor[0-9]* 2>/dev/null; } | head -3)"
  [ -z "$out" ] || st WARN "the tutorial held output from a run in the checkout; clean it first: pgf run $(pgf_tilde "$dest") Allclean"
  st_next "pgf run $(pgf_tilde "$dest")" "run it with the build of $(pgf_checkout_name)"
}

# pgf_copy_check — in a case dir copied by pgf copy: INFO where it came from,
#   WARN when that checkout has moved on since.
pgf_copy_check() {
  local from info warn
  IFS=$'\t' read -r from info warn < <(python3 "$PGF_LIB/copies.py" check . 2>/dev/null) || return 0
  [ -n "$info" ] || return 0
  st INFO "$info"
  [ -z "$warn" ] || st WARN "$warn"
}
