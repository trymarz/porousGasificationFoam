# utilities/bash_utils/helpers.sh — clog, kept for the build and regression scripts;
# it prints pgf's one status style (utilities/pgf/ui.sh, #285).
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "helpers.sh is a source-only library — do not execute it directly." >&2
    echo "Use: source utilities/bash_utils/helpers.sh" >&2
    exit 1
fi

[[ -n "${HELPERS_SOURCED:-}" ]] && return
HELPERS_SOURCED=1
. "$(dirname "${BASH_SOURCE[0]}")/../pgf/ui.sh"

# Enable/disable timestamps (default: disabled)
ENABLE_LOG_TIMESTAMPS=false

# clog LEVEL MESSAGE — INFO, SUCCESS (OK), WARNING|WARN, ERROR (FAIL, stderr),
#   SKIP, DEBUG (INFO); anything else is an INFO line of all its words.
clog() {
  local level="${1:-}" message timestamp=""
  shift
  message="$*"
  [[ "$ENABLE_LOG_TIMESTAMPS" == true ]] && timestamp="$(date '+%Y-%m-%d %H:%M:%S') - "
  case "$level" in
  INFO | DEBUG) st INFO "${timestamp}${message}" ;;
  SUCCESS) st OK "${timestamp}${message}" ;;
  WARNING | WARN) st WARN "${timestamp}${message}" ;;
  ERROR) st FAIL "${timestamp}${message}" >&2 ;;
  SKIP) st SKIP "${timestamp}${message}" ;;
  *) st INFO "${timestamp}${level}${message:+ $message}" ;;
  esac
}
