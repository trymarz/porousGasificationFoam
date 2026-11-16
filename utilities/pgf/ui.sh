#!/bin/bash
# ui.sh — pgf's one output style (#285), source-only: "  LABEL message" lines
# (OK WARN FAIL SKIP INFO PASS), headings and the next: block. Colour only on a
# terminal: PGF_COLOR=auto|always|never (pgf --color), NO_COLOR turns auto off.

[[ -n "${PGF_UI_SOURCED:-}" ]] && return 0
PGF_UI_SOURCED=1

# pgf_ui_color — 0 when stdout gets colour (a caller's >&2 makes stdout stderr).
pgf_ui_color() {
  case "${PGF_COLOR:-auto}" in
  always) return 0 ;;
  never) return 1 ;;
  esac
  [ -z "${NO_COLOR:-}" ] && [ -t 1 ] && [ "${TERM:-}" != dumb ]
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
