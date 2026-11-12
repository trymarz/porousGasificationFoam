#!/bin/bash
# mpi.sh — PGF_MPI=auto (#257): decide whether Open MPI must stay on lo.
# Source-only. PGF_ENV_SYSFS_NET (default /sys/class/net) is a test hook.

# pgf_mpi_ifaces — "name state" per non-loopback interface.
pgf_mpi_ifaces() {
  local d n
  for d in "${PGF_ENV_SYSFS_NET:-/sys/class/net}"/*; do
    n="${d##*/}"
    [ "$n" = lo ] || [ ! -e "$d" ] && continue
    printf '%s %s\n' "$n" "$(cat "$d/operstate" 2>/dev/null || echo unknown)"
  done
}

# pgf_mpi_auto — print the reason and return 0 when a single-host run should
#   use loopback: no usable interface, several, or a container/VM bridge.
pgf_mpi_auto() {
  local name state up=() virt=()
  while read -r name state; do
    [ "$state" = down ] && continue
    up+=("$name")
    case "$name" in docker* | virbr* | br-* | veth*) virt+=("$name") ;; esac
  done < <(pgf_mpi_ifaces)
  if [ ${#virt[@]} -gt 0 ]; then
    echo "bridge/virtual interfaces up: ${virt[*]}"
  elif [ ${#up[@]} -gt 1 ]; then
    echo "${#up[@]} interfaces up: ${up[*]}"
  elif [ ${#up[@]} -eq 0 ]; then
    echo "no interface up besides lo"
  else
    echo "one interface up: ${up[0]}"
    return 1
  fi
}
