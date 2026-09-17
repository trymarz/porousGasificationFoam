#!/bin/bash
# Install the tracked git hooks from scripts/hooks/ into this clone.
#
#     ./scripts/install-hooks.sh          install (refuses to clobber)
#     ./scripts/install-hooks.sh --force  overwrite an existing hook
#     ./scripts/install-hooks.sh --list   show what is installed now
#     ./scripts/install-hooks.sh --remove uninstall hooks this script installed
#
# Hooks are per-clone and git never installs them for you, which is why the
# source lives in the repository and this script copies it into place. Copying
# rather than symlinking is deliberate: a symlink into the worktree would run
# whatever the checked-out branch happens to contain, so switching to a branch
# that changes the hook would silently change what gates your push.
#
# Worktrees share the main repository's hooks directory, so installing from any
# worktree installs for all of them.

set -eu

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SRC_DIR="$SCRIPT_DIR/hooks"

# --git-path resolves correctly from a linked worktree, where .git is a file.
HOOK_DIR="$(git rev-parse --git-path hooks)"
case "$HOOK_DIR" in
/*) ;;
*) HOOK_DIR="$(git rev-parse --show-toplevel)/$HOOK_DIR" ;;
esac

MARKER='# managed by scripts/install-hooks.sh'
MODE="install"

while [ $# -gt 0 ]; do
  case "$1" in
  --force) MODE="force" ;;
  --list) MODE="list" ;;
  --remove) MODE="remove" ;;
  -h | --help)
    sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *)
    echo "install-hooks.sh: unknown option '$1'" >&2
    exit 2
    ;;
  esac
  shift
done

[ -d "$SRC_DIR" ] || {
  echo "install-hooks.sh: no hook sources at $SRC_DIR" >&2
  exit 1
}

mkdir -p "$HOOK_DIR"

for src in "$SRC_DIR"/*; do
  [ -f "$src" ] || continue
  name="$(basename "$src")"
  dst="$HOOK_DIR/$name"

  case "$MODE" in
  list)
    if [ ! -e "$dst" ]; then
      printf '  %-12s not installed\n' "$name"
    elif grep -qF "$MARKER" "$dst" 2>/dev/null; then
      if cmp -s <(grep -vxF "$MARKER" "$dst") "$src"; then
        printf '  %-12s installed, up to date\n' "$name"
      else
        printf '  %-12s installed, DIFFERS from scripts/hooks/%s\n' "$name" "$name"
      fi
    else
      printf '  %-12s present, not managed by this script\n' "$name"
    fi
    continue
    ;;
  remove)
    if [ -e "$dst" ] && grep -qF "$MARKER" "$dst" 2>/dev/null; then
      rm -f "$dst"
      echo "removed $dst"
    elif [ -e "$dst" ]; then
      echo "left $dst alone (not installed by this script)"
    fi
    continue
    ;;
  esac

  # install / force
  if [ -e "$dst" ] && [ "$MODE" != "force" ] &&
    ! grep -qF "$MARKER" "$dst" 2>/dev/null; then
    echo "install-hooks.sh: $dst already exists and was not installed by this" >&2
    echo "  script. Inspect it, then re-run with --force to replace it." >&2
    exit 1
  fi

  # The marker goes after the shebang so the hook stays executable, and lets
  # --list and --remove tell our copy from one someone wrote by hand.
  {
    head -n 1 "$src"
    printf '%s\n' "$MARKER"
    tail -n +2 "$src"
  } >"$dst"
  chmod +x "$dst"
  echo "installed $dst"
done

if [ "$MODE" = "install" ] || [ "$MODE" = "force" ]; then
  echo
  echo "Hooks installed into $HOOK_DIR"
  echo "Skip one push with: git push --no-verify"
fi
