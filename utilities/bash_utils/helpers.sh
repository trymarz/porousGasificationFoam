# utilis/helpers.sh
[[ -n "$HELPERS_SOURCED" ]] && return
HELPERS_SOURCED=1

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Enable/disable timestamps (default: disabled)
ENABLE_LOG_TIMESTAMPS=false

# Logging function
clog() {

  local level=$1
  shift
  local message="$*"
  local timestamp=""

  # Add timestamp only if enabled
  if [[ "$ENABLE_LOG_TIMESTAMPS" == true ]]; then
    timestamp="$(date '+%Y-%m-%d %H:%M:%S') - "
  fi

  case $level in
  info)
    echo -e "${BLUE}[INFO]${NC} ${timestamp}${message}"
    ;;
  success)
    echo -e "${GREEN}[✓ SUCCESS]${NC} ${timestamp}${message}"
    ;;
  warning)
    echo -e "${YELLOW}[⚠ WARNING]${NC} ${timestamp}${message}"
    ;;
  error)
    echo -e "${RED}[✗ ERROR]${NC} ${timestamp}${message}" >&2
    ;;
  debug)
    echo -e "${MAGENTA}[DEBUG]${NC} ${timestamp}${message}"
    ;;
  *)
    echo -e "${CYAN}[LOG]${NC} ${timestamp}${message}"
    ;;
  esac
}

error_exit() {
  clog error "$1"
  exit 1
}

# Completion function for the log command
_clog_completion() {
  local cur="${COMP_WORDS[COMP_CWORD]}"

  # If completing the first argument (log level)
  if [[ ${COMP_CWORD} -eq 1 ]]; then
    local levels="info success warning error debug"
    COMPREPLY=($(compgen -W "$levels" -- "$cur"))
  fi
}

# Register the completion function
complete -F _clog_completion clog
