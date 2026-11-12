#!/bin/bash
# config.sh — pgf-env's layered settings (#257; doc/build.md). Source-only.
# Later wins: default, user env.conf, checkout .pgf-env.conf, environment, CLI.
# Files are parsed, never sourced, and only PGF_CONF_KEYS are accepted.

PGF_CONF_KEYS=(PGF_FOAM_BASHRC YADE_TRUNK YADE_BIN YADE_PYTHONPATH PGF_YADE_LIBDIR
  PGF_BUILD_ROOT PGF_RUN_ROOT PGF_YADE PGF_MPI)

# Resolved value and its layer per key: PGF_CONF_VAL[k], PGF_CONF_SRC[k].
declare -gA PGF_CONF_VAL=() PGF_CONF_SRC=() PGF_CONF_CLI=()

pgf_conf_user_file() {
  printf '%s/pgf/env.conf' "${XDG_CONFIG_HOME:-${HOME:-}/.config}"
}

pgf_conf_checkout_file() {
  printf '%s/.pgf-env.conf' "$PGF_ROOT"
}

pgf_conf_known_key() {
  local k
  for k in "${PGF_CONF_KEYS[@]}"; do [ "$k" = "$1" ] && return 0; done
  return 1
}

# pgf_conf_check_value KEY VALUE — enumerated keys only take their values.
pgf_conf_check_value() {
  case "$1" in
  PGF_YADE) [[ "$2" =~ ^[01]$ ]] || { echo "PGF_YADE must be 0 or 1, not '$2'"; return 1; } ;;
  PGF_MPI) [[ "$2" =~ ^(auto|loopback|default)$ ]] ||
    { echo "PGF_MPI must be auto, loopback or default, not '$2'"; return 1; } ;;
  esac
  return 0
}

# pgf_conf_parse FILE LAYER — fold FILE into PGF_CONF_VAL; on a bad line set
#   PGF_CONF_ERR to "FILE:LINE: reason" and return 1.
pgf_conf_parse() {
  local file="$1" layer="$2" line n=0 key val why
  [ -e "$file" ] || return 0
  [ -r "$file" ] || { PGF_CONF_ERR="$file: not readable"; return 1; }
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1))
    line="${line%$'\r'}"
    [[ "$line" =~ ^[[:space:]]*(#.*)?$ ]] && continue
    [[ "$line" =~ ^([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]] ||
      { PGF_CONF_ERR="$file:$n: expected KEY=value, got '$line'"; return 1; }
    key="${BASH_REMATCH[1]}" val="${BASH_REMATCH[2]}"
    pgf_conf_known_key "$key" || { PGF_CONF_ERR="$file:$n: unknown key '$key'"; return 1; }
    if [[ "$val" =~ ^\"(.*)\"$ ]] || [[ "$val" =~ ^\'(.*)\'$ ]]; then val="${BASH_REMATCH[1]}"; fi
    [[ "$val" == "~/"* ]] && val="$HOME/${val:2}"
    why="$(pgf_conf_check_value "$key" "$val")" || { PGF_CONF_ERR="$file:$n: $why"; return 1; }
    PGF_CONF_VAL[$key]="$val"
    PGF_CONF_SRC[$key]="$layer $file:$n"
  done <"$file"
}

# pgf_conf_resolve — run all layers and export every key that ends up set; on
#   error set PGF_CONF_ERR and return 1. Runs after env -i, so "env" means the
#   allowlisted caller variables.
pgf_conf_resolve() {
  local k msg
  PGF_CONF_VAL=() PGF_CONF_SRC=()
  PGF_CONF_VAL[PGF_YADE]=0 PGF_CONF_SRC[PGF_YADE]="default"
  PGF_CONF_VAL[PGF_MPI]=default PGF_CONF_SRC[PGF_MPI]="default"
  pgf_conf_parse "$(pgf_conf_user_file)" user || return 1
  pgf_conf_parse "$(pgf_conf_checkout_file)" checkout || return 1
  for k in "${PGF_CONF_KEYS[@]}"; do
    if [ -n "${PGF_CONF_CLI[$k]+x}" ]; then
      PGF_CONF_VAL[$k]="${PGF_CONF_CLI[$k]}" PGF_CONF_SRC[$k]="cli"
    elif [ -n "${!k+x}" ]; then
      msg="$(pgf_conf_check_value "$k" "${!k}")" || { PGF_CONF_ERR="environment: $msg"; return 1; }
      PGF_CONF_VAL[$k]="${!k}" PGF_CONF_SRC[$k]="env"
    fi
    if [ -n "${PGF_CONF_VAL[$k]+x}" ]; then
      export "$k=${PGF_CONF_VAL[$k]}"
    fi
  done
}

# pgf_conf_set_cli KEY=VALUE — the CLI layer (pgf-env --set).
pgf_conf_set_cli() {
  local key="${1%%=*}" val="${1#*=}" why
  [ "$key" != "$1" ] || { echo "--set needs KEY=VALUE, got '$1'"; return 1; }
  pgf_conf_known_key "$key" || { echo "--set: unknown key '$key' (known: ${PGF_CONF_KEYS[*]})"; return 1; }
  why="$(pgf_conf_check_value "$key" "$val")" || { echo "--set: $why"; return 1; }
  PGF_CONF_CLI[$key]="$val"
}

# pgf_conf_print — the effective values and where each came from.
pgf_conf_print() {
  local k
  printf 'config files:\n  user:     %s%s\n  checkout: %s%s\n' \
    "$(pgf_conf_user_file)" "$([ -e "$(pgf_conf_user_file)" ] || echo ' (absent)')" \
    "$(pgf_conf_checkout_file)" "$([ -e "$(pgf_conf_checkout_file)" ] || echo ' (absent)')"
  echo "settings:"
  for k in "${PGF_CONF_KEYS[@]}"; do
    if [ -n "${PGF_CONF_VAL[$k]+x}" ]; then
      printf '  %-16s %-40s [%s]\n' "$k" "${PGF_CONF_VAL[$k]}" "${PGF_CONF_SRC[$k]}"
    else
      printf '  %-16s %-40s [%s]\n' "$k" "-" "unset"
    fi
  done
}
