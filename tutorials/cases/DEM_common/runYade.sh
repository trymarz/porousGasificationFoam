# runYade.sh — sourced by the DEM tutorials' Allrun. POSIX sh: the Allruns
# are #!/bin/sh, which has no pipefail.
#
# runYade CMD [ARGS ...]
#   Runs CMD (the coupled `mpirun ... yade <script>.py`) with its output shown
#   and written to log.yade, and returns CMD's exit status, not tee's. A run
#   that exits 0 but logged "FOAM FATAL" (a spawned solver that aborted under
#   an mpirun that still reported success) returns 1. Use as
#       runYade mpirun -n 2 yade MPI_lambda.py || exit
#   so the Allrun stops, with that status, before its post-run checks.

runYade() {
    { "$@" 2>&1; echo $? > .yade.status; } | tee log.yade
    runYade_status=$(cat .yade.status 2>/dev/null)
    rm -f .yade.status
    if [ "${runYade_status:-1}" = 0 ] && grep -q "FOAM FATAL" log.yade; then
        echo "runYade: log.yade has FOAM FATAL although the run exited 0" >&2
        runYade_status=1
    fi
    return "${runYade_status:-1}"
}
