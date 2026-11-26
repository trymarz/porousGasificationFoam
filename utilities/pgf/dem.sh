#!/bin/bash
# dem.sh — pgf dem: coupled Yade + porousGasificationFoam runs (#328, #340).
# what:    pgf dem run starts both sides in one mpirun, from the case directory
# why:     one launcher inside the pgf CLI instead of the loose pgfDemRun script
#
#   pgf dem run [-ndem N] [-nfoam M] [-solver NAME] SCRIPT.py [yade args ...]
#
# One mpirun of N+M copies of this script; ranks below N exec `yade SCRIPT.py`,
# the others `SOLVER -parallel -mpi-split-by-appnum -mpi-threads`. One app, not
# `A : B`: Open MPI 5.0.7 segfaults on that (#328). The help page is in cli.sh.
# Exit 2 on a usage or case error; otherwise mpirun's status.

self="$(readlink -f -- "${BASH_SOURCE[0]}")"
PGF_ROOT="${PGF_ROOT:-$(cd "$(dirname "$self")/../.." && pwd)}"

dem_die() {
  echo "pgf dem run: $1" >&2
  exit 2
}

# Per-rank role switch: dem.sh --role NDEM SOLVER yade SCRIPT.py [args ...]
if [ "${1:-}" = --role ]; then
  ndem="$2" solver="$3"
  shift 3
  rank="${OMPI_COMM_WORLD_RANK:-${PMIX_RANK:-${PMI_RANK:-${SLURM_PROCID:-}}}}"
  [ -n "$rank" ] || dem_die "cannot find this process's MPI rank (no OMPI_COMM_WORLD_RANK, PMIX_RANK, PMI_RANK or SLURM_PROCID)"
  [ "$rank" -lt "$ndem" ] && exec "$@"
  exec "$solver" -parallel -mpi-split-by-appnum -mpi-threads
fi

dem_help() { exec "$PGF_ROOT/pgf" help dem; }

case "${1:-}" in
run) shift ;;
"" | -h | --help | help) dem_help ;;
*) echo "pgf: unknown dem command '$1' (see pgf help dem)" >&2; exit 2 ;;
esac

ndem=2 nfoam="" solver=porousGasificationFoam
while [ $# -gt 0 ]; do
  case "$1" in
  -ndem) ndem="${2:?-ndem needs a number}"; shift 2 ;;
  -nfoam) nfoam="${2:?-nfoam needs a number}"; shift 2 ;;
  -solver) solver="${2:?-solver needs a name}"; shift 2 ;;
  -h | -help | --help) dem_help ;;
  -*) dem_die "unknown option $1 (see pgf help dem)" ;;
  *) break ;;
  esac
done
[ $# -ge 1 ] || dem_die "no Yade script given (see pgf help dem)"
[ -f "$1" ] || dem_die "Yade script $1 not found in $PWD"

if [ -z "$nfoam" ]; then
  [ -f system/decomposeParDict ] || dem_die "no system/decomposeParDict: give -nfoam"
  nfoam="$(foamDictionary -entry numberOfSubdomains -value system/decomposeParDict 2>/dev/null)" ||
    dem_die "cannot read numberOfSubdomains from system/decomposeParDict"
fi
case "$ndem$nfoam" in *[!0-9]*) dem_die "-ndem and -nfoam must be whole numbers (got $ndem, $nfoam)" ;; esac
[ "$ndem" -ge 1 ] && [ "$nfoam" -ge 1 ] || dem_die "-ndem and -nfoam must be at least 1"
# The solver always runs -parallel, so even one OpenFOAM rank reads processor0 (#328 D4).
[ -d "processor$((nfoam - 1))" ] && [ ! -d "processor$nfoam" ] ||
  dem_die "the case is not decomposed into $nfoam processor* directories (run decomposePar, numberOfSubdomains $nfoam)"

flags=""
if mpirun --version 2>&1 | grep -q "Open MPI\|OpenRTE"; then
  flags="--oversubscribe"
  [ "$(id -u)" = 0 ] && flags="$flags --allow-run-as-root"
fi
np=$((ndem + nfoam))
echo "pgf dem run: $ndem Yade + $nfoam OpenFOAM rank(s): mpirun -np $np $flags ${PGF_MPIRUN_FLAGS:-}"
# shellcheck disable=SC2086 # the flags are words
exec mpirun $flags ${PGF_MPIRUN_FLAGS:-} -np "$np" "$self" --role "$ndem" "$solver" yade "$@"
