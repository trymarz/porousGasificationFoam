# runYade.sh — sourced by the DEM tutorials' Allrun. POSIX sh: the Allruns
# are #!/bin/sh, which has no pipefail.
#
# runYade CMD [ARGS ...]
#   Runs CMD (the coupled `pgf dem run ... <script>.py`) with its output in
#   log.yade, as runApplication keeps log.*, and returns CMD's exit status. A run
#   that exits 0 but logged "FOAM FATAL" (a solver that aborted under an
#   mpirun that still reported success) returns 1. Use as
#       runYade "${PGF_ROOT:-../../..}/pgf" dem run -ndem 2 MPI_lambda.py || exit
#   so the Allrun stops, with that status, before its post-run checks.

runYade() {
    "$@" > log.yade 2>&1
    runYade_status=$?
    if [ "${runYade_status:-1}" = 0 ] && grep -q "FOAM FATAL" log.yade; then
        echo "runYade: log.yade has FOAM FATAL although the run exited 0" >&2
        runYade_status=1
    fi
    return "${runYade_status:-1}"
}
