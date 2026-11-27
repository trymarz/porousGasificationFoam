#!/bin/bash
#
# pgf-fresh.sh: a throwaway Ubuntu 26.04 "fresh computer" in a rootless Podman
# container, for walking through doc/install.md the way a new user meets it.
#
#   container/pgf-fresh.sh create        make the container and its home
#   container/pgf-fresh.sh shell         open a login shell inside (starts it if stopped)
#   container/pgf-fresh.sh run CMD...    run one command inside, in a login shell
#   container/pgf-fresh.sh status        show the container, image and home
#   container/pgf-fresh.sh stop          stop it (keeps everything)
#   container/pgf-fresh.sh rm [--home]   remove the container; --home also deletes its home
#
# Isolation: nothing of the host is shared except PGF_FRESH_HOME (mounted as the
# container user's home, /home/ubuntu) and the X11 socket directory, so GUI apps
# such as ParaView can open. No host environment is copied in, /tmp is the
# container's own, and the host home is not mounted. The user is the image's
# "ubuntu" (uid 1000, mapped to the caller with --userns=keep-id) with
# passwordless sudo inside the container only. Settings (environment):
#   PGF_FRESH_NAME   container name            (default pgf-fresh)
#   PGF_FRESH_IMAGE  base image                (default docker.io/library/ubuntu:26.04,
#                                               the Ubuntu of container/pgf-toolchain.def)
#   PGF_FRESH_HOME   host folder for its home  (default ~/pgf-fresh-home)
# Exit codes: 0 on success, 2 on a usage or setup error, else the inner command's.

set -u

NAME="${PGF_FRESH_NAME:-pgf-fresh}"
IMAGE="${PGF_FRESH_IMAGE:-docker.io/library/ubuntu:26.04}"
FRESH_HOME="${PGF_FRESH_HOME:-$HOME/pgf-fresh-home}"
CUSER=ubuntu
CHOME=/home/$CUSER
XAUTH_IN=/tmp/.pgf-fresh-xauth

die() {
  echo "pgf-fresh: $*" >&2
  exit 2
}

usage() {
  sed -n '3,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

exists() { podman container exists "$NAME"; }

running() {
  [ "$(podman container inspect -f '{{.State.Running}}' "$NAME" 2>/dev/null)" = true ]
}

as_root() { podman exec -u root "$NAME" "$@"; }

cmd_create() {
  command -v podman >/dev/null || die "podman not found"
  exists && die "container '$NAME' already exists (pgf-fresh.sh rm first)"
  mkdir -p "$FRESH_HOME" || die "cannot create $FRESH_HOME"
  local x11=()
  [ -d /tmp/.X11-unix ] && x11=(-v /tmp/.X11-unix:/tmp/.X11-unix:ro)
  podman create --name "$NAME" --hostname "$NAME" --init \
    --userns=keep-id:uid=1000,gid=1000 \
    -v "$FRESH_HOME:$CHOME" "${x11[@]}" \
    -e LANG=C.UTF-8 \
    "$IMAGE" sleep infinity >/dev/null || die "podman create failed"
  podman start "$NAME" >/dev/null || die "podman start failed"
  # A fresh Ubuntu desktop has a sudo-capable user; the image has the user but no sudo.
  as_root sh -c "getent passwd 1000 >/dev/null || useradd -u 1000 -d $CHOME -s /bin/bash $CUSER" \
    || die "cannot set up user $CUSER"
  as_root sh -c "apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends sudo >/dev/null" \
    || die "cannot install sudo"
  as_root sh -c "echo '$CUSER ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/pgf-fresh && chmod 440 /etc/sudoers.d/pgf-fresh" \
    || die "cannot configure sudo"
  podman exec -u "$CUSER" "$NAME" sh -c "[ -e $CHOME/.bashrc ] || cp -r /etc/skel/. $CHOME/" \
    || die "cannot populate $CHOME"
  echo "pgf-fresh: created '$NAME' from $IMAGE; home $FRESH_HOME -> $CHOME"
}

# Copy the caller's X cookie in as a wildcard entry, so it matches the container's hostname.
share_display() {
  [ -n "${DISPLAY:-}" ] && command -v xauth >/dev/null || return 0
  local tmp
  tmp="$(mktemp)" || return 0
  xauth nlist "$DISPLAY" 2>/dev/null | sed 's/^..../ffff/' | xauth -f "$tmp" nmerge - 2>/dev/null
  podman cp "$tmp" "$NAME:$XAUTH_IN" >/dev/null 2>&1 \
    && as_root sh -c "chown 1000:1000 $XAUTH_IN && chmod 600 $XAUTH_IN"
  rm -f "$tmp"
}

enter() {
  exists || die "no container '$NAME' (run: pgf-fresh.sh create)"
  running || podman start "$NAME" >/dev/null || die "podman start failed"
  share_display
  local tty=(-i)
  [ -t 0 ] && [ -t 1 ] && tty=(-it)
  exec podman exec "${tty[@]}" -u "$CUSER" -w "$CHOME" \
    -e HOME="$CHOME" -e USER="$CUSER" -e TERM="${TERM:-xterm}" \
    -e DISPLAY="${DISPLAY:-}" -e XAUTHORITY="$XAUTH_IN" \
    "$NAME" "$@"
}

cmd_rm() {
  local home=0
  [ "${1:-}" = --home ] && home=1
  exists && { podman rm -f "$NAME" >/dev/null || die "podman rm failed"; }
  echo "pgf-fresh: container '$NAME' removed"
  [ "$home" = 1 ] || { echo "pgf-fresh: home kept: $FRESH_HOME"; return 0; }
  [ -d "$FRESH_HOME" ] || return 0
  read -r -p "Delete $FRESH_HOME and everything in it? [y/N] " ans
  [ "$ans" = y ] || [ "$ans" = Y ] || { echo "pgf-fresh: home kept"; return 0; }
  # podman unshare: files written as root inside the container belong to a subuid.
  podman unshare rm -rf -- "$FRESH_HOME" || die "cannot delete $FRESH_HOME"
  echo "pgf-fresh: deleted $FRESH_HOME"
}

cmd_status() {
  echo "name:  $NAME"
  echo "image: $IMAGE"
  echo "home:  $FRESH_HOME -> $CHOME"
  if exists; then
    podman ps -a --filter "name=^${NAME}\$" --format 'state: {{.State}} ({{.Status}})'
  else
    echo "state: not created"
  fi
}

case "${1:-}" in
  create) cmd_create ;;
  shell) enter bash -l ;;
  run) shift; [ $# -gt 0 ] || die "run needs a command"; enter bash -lc 'exec "$@"' bash "$@" ;;
  status) cmd_status ;;
  stop) exists || die "no container '$NAME'"; podman stop "$NAME" >/dev/null && echo "pgf-fresh: '$NAME' stopped" ;;
  rm) shift; cmd_rm "$@" ;;
  -h|--help|help) usage ;;
  *) usage >&2; exit 2 ;;
esac
