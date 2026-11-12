#!/bin/bash
# commands.sh — pgf-env's init / install-link helpers (#257). Source-only;
# needs config.sh, PGF_ROOT, SELF and PGF_ENV_CALLER_PATH.

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
  local layer="$1" file k v tmp
  PGF_INIT_ASK=0
  [ "$2" -eq 0 ] && [ -t 0 ] && [ -r /dev/tty ] && PGF_INIT_ASK=1
  if [ "$layer" = "checkout" ]; then file="$(pgf_conf_checkout_file)"; else file="$(pgf_conf_user_file)"; fi

  PGF_CONF_VAL=() PGF_CONF_SRC=()
  pgf_conf_parse "$file" "$layer" || { echo "pgf-env: $PGF_CONF_ERR" >&2; return 1; }
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

  mkdir -p "${file%/*}" || return 1
  tmp="$file.tmp.$$"
  {
    echo "# pgf-env settings ($layer layer); see doc/build.md. KEY=value, parsed not sourced."
    for k in "${PGF_CONF_KEYS[@]}"; do
      if [ -n "${out[$k]+x}" ]; then printf '%s=%s\n' "$k" "${out[$k]}"; fi
    done
  } >"$tmp" && mv -f "$tmp" "$file" || { rm -f "$tmp"; return 1; }
  echo "pgf-env: wrote $file" >&2
  [ -n "${out[PGF_FOAM_BASHRC]:-}" ] ||
    echo "pgf-env: no OpenFOAM bashrc found; add PGF_FOAM_BASHRC=... to $file" >&2
}

# pgf_install_link FORCE — ~/.local/bin/pgf-env -> this checkout's pgf-env.
pgf_install_link() {
  local link="$HOME/.local/bin/pgf-env" cur
  if [ -L "$link" ] || [ -e "$link" ]; then
    cur="$(readlink -f "$link")"
    if [ "$cur" = "$SELF" ]; then
      echo "pgf-env: $link already points here" >&2
      return 0
    fi
    [ "$1" -eq 1 ] || {
      echo "pgf-env: $link points at ${cur:-a file}; rerun with --force to replace it" >&2
      echo "pgf-env: (with several worktrees, the link picks one; call the others by path)" >&2
      return 2
    }
  fi
  mkdir -p "${link%/*}" && ln -sfn "$SELF" "$link" || return 2
  echo "pgf-env: $link -> $SELF" >&2
  case ":$PGF_ENV_CALLER_PATH:" in
  *":${link%/*}:"*) ;;
  *) echo "pgf-env: note: ${link%/*} is not on your PATH" >&2 ;;
  esac
}

# pgf_doctor — read-only checks, one "OK|WARN|FAIL message" line each; returns
#   1 when any FAIL. Runs before OpenFOAM is sourced, so it sources in a child.
pgf_doctor() {
  local fails=0 bashrc info ver v p yb out pin trunk why
  _r() {
    printf '  %-4s %s\n' "$1" "$2"
    [ "$1" = FAIL ] && fails=$((fails + 1))
    return 0
  }
  echo "checks:"
  bashrc="${PGF_FOAM_BASHRC:-${WM_PROJECT_DIR:+$WM_PROJECT_DIR/etc/bashrc}}"
  if [ -n "$bashrc" ] && [ -r "$bashrc" ]; then
    info="$(env -i HOME="$HOME" PATH=/usr/bin:/bin bash -c 'f="$1"; set +eu; set --; . "$f" >/dev/null 2>&1; echo "$WM_PROJECT_VERSION"' _ "$bashrc")"
    ver="${info##*$'\n'}"
    if [ -z "$ver" ]; then
      _r FAIL "$bashrc set no WM_PROJECT_VERSION; fix: pgf-env init --foam-bashrc <OpenFOAM-v2406>/etc/bashrc"
    elif [ "$ver" != v2406 ]; then
      _r WARN "OpenFOAM $ver from $bashrc; the solver targets v2406"
    else
      _r OK "OpenFOAM $ver from $bashrc"
    fi
    for v in 0 1; do
      [ "$v" -eq 1 ] && [ "${PGF_YADE:-0}" != 1 ] && continue
      local flag=()
      [ "$v" -eq 1 ] && flag=(--yade)
      p="$("$SELF" --bare --mpi default -- "$PGF_ROOT/build.sh" --print-prefix "${flag[@]}" 2>/dev/null | tail -1)"
      if [ -n "$p" ] && [ -f "$p/.pgf-build.json" ]; then
        _r OK "yade$v build present: $p"
      else
        _r WARN "no yade$v build for this tree${p:+ at $p}; fix: $PGF_ROOT/pgf-env --bare${flag:+ --yade} -- $PGF_ROOT/build.sh --isolated${flag:+ --yade}"
      fi
    done
  else
    _r FAIL "no readable OpenFOAM bashrc (${bashrc:-unset}); fix: pgf-env init, or --foam-bashrc FILE"
  fi

  yb="${YADE_BIN:-$(PATH="$PGF_ENV_CALLER_PATH" command -v yade)}"
  if [ -z "$yb" ] && [ "${PGF_YADE:-0}" != 1 ]; then
    _r OK "yade: not configured (only needed for --yade)"
  elif [ ! -x "$yb" ]; then
    _r FAIL "no yade binary (${yb:-none}); fix: pgf-env init --yade-bin PATH (see doc/build.md, YADE DEM coupling)"
  else
    out="$(timeout 60 "$yb" -v 2>/dev/null | grep -m1 -i 'version')"
    _r OK "yade $yb: ${out:-version unknown}"
    pin="$(pgf_coupling_pin)"
    if [[ "$out" =~ git-([0-9a-f]{7,}) ]] && [ -n "$pin" ] && [[ "$pin" != "${BASH_REMATCH[1]}"* ]]; then
      _r WARN "yade was built from ${BASH_REMATCH[1]}, the submodule pin is ${pin:0:10}; the MPI buffer layout is not negotiated"
    fi
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

  p="$(PATH="$PGF_ENV_CALLER_PATH" command -v pgf-env)"
  if [ -z "$p" ]; then
    _r WARN "pgf-env is not on PATH; fix: $SELF install-link"
  elif [ "$(readlink -f "$p")" != "$SELF" ]; then
    _r WARN "pgf-env on PATH is $(readlink -f "$p"), not this checkout; call this one by path"
  else
    _r OK "pgf-env on PATH is this checkout's"
  fi
  [ "$fails" -eq 0 ]
}
