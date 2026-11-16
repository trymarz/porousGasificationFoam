#!/bin/bash
# ui.sh — pgf's one output style (#285), source-only: "  LABEL message" lines
# (OK WARN FAIL SKIP INFO PASS), "→ step" lines, headings and the next: block.
# Colour: PGF_COLOR=auto|always|never (pgf --color); auto means on when both
# stdout and stderr are a terminal, TERM is not dumb and NO_COLOR is unset.

[[ -n "${PGF_UI_SOURCED:-}" ]] && return 0
PGF_UI_SOURCED=1

# Decided once, here: inside $(...) stdout is a pipe, so [ -t 1 ] there says no (#287).
case "${PGF_COLOR:-auto}" in
always) PGF_UI_COLOR=1 ;;
never) PGF_UI_COLOR=0 ;;
*) if [ -z "${NO_COLOR:-}" ] && [ -t 1 ] && [ -t 2 ] && [ "${TERM:-dumb}" != dumb ]; then
    PGF_UI_COLOR=1
  else PGF_UI_COLOR=0; fi ;;
esac

# pgf_ui_color — 0 when pgf's output gets colour.
pgf_ui_color() {
  [ "$PGF_UI_COLOR" = 1 ]
}

# pgf_tilde PATH — PATH with $HOME shown as ~.
pgf_tilde() {
  case "$1" in "${HOME:-/nonexistent}") printf '~' ;; "${HOME:-/nonexistent}"/*) printf '~/%s' "${1#"$HOME"/}" ;; *) printf '%s' "$1" ;; esac
}

# pgf_ui_paint SGR TEXT — TEXT in that SGR style (32 green, 1 bold, 2 dim ...).
pgf_ui_paint() {
  if pgf_ui_color; then printf '\033[%sm%s\033[0m' "$1" "$2"; else printf '%s' "$2"; fi
}

# st LABEL MESSAGE [DETAIL] — one status line; DETAIL (a path, a hint) is dimmed.
st() {
  local sgr
  case "$1" in
  OK | PASS) sgr=32 ;;
  WARN) sgr=33 ;;
  FAIL | FATAL) sgr='1;31' ;;
  INFO) sgr=36 ;;
  *) sgr=2 ;;
  esac
  printf '  %s %s' "$(pgf_ui_paint "$sgr" "$(printf '%-4s' "$1")")" "$2"
  [ -z "${3:-}" ] || printf ' %s' "$(pgf_ui_paint 2 "$3")"
  printf '\n'
}

# st_check LEVEL "MESSAGE[; fix: COMMAND]" — st, with the fix on a dimmed line
#   below; an OK line drops it.
st_check() {
  local msg="${2%%; fix: *}"
  st "$1" "$msg"
  [ "$msg" = "$2" ] || [ "$1" = OK ] || st_more "fix: ${2#*; fix: }"
}

# st_step TEXT LABEL [DETAIL] — one "→ TEXT … LABEL DETAIL" line of a command's progress.
st_step() {
  local sgr
  case "$2" in
  OK) sgr=32 ;;
  WARN) sgr=33 ;;
  FAIL) sgr='1;31' ;;
  *) sgr=2 ;;
  esac
  printf '  %s %s … %s%s\n' "$(pgf_ui_paint 36 "→")" "$1" "$(pgf_ui_paint "$sgr" "$2")" "${3:+ $3}"
}

# st_more TEXT — a continuation line under the message column.
st_more() {
  printf '       %s\n' "$(pgf_ui_paint 2 "$1")"
}

# st_head TEXT — a bold section heading.
st_head() {
  printf '%s\n' "$(pgf_ui_paint 1 "$1")"
}

# st_next CMD WHAT [CMD WHAT ...] — the closing "next:" block, commands aligned.
st_next() {
  local w=0 a i
  for ((i = 1; i <= $#; i += 2)); do a="${!i}"; [ ${#a} -gt "$w" ] && w=${#a}; done
  st_head "next:"
  while [ $# -gt 0 ]; do
    printf '  %s  %s\n' "$(pgf_ui_paint 1 "$(printf "%-${w}s" "$1")")" "${2:-}"
    shift 2 || shift
  done
}
