#!/bin/bash
# Run one regression case and compare its postProcessing/ output against the
# committed reference/postProcessing/ baseline.
#
# Usage:
#   runCase.sh <caseDir> [--no-run] [--rtol R] [--atol A] [--compare PATH]
#
# Exit codes (shared outcome taxonomy — see tools/regressionLib.sh):
#   0      - PASS  (ran, compared, within tolerance)
#   1      - FAIL  (ran, compared, numerical divergence)
#   2      - ERROR (infrastructure: missing files, dirty slate, run failure)
#   128+N  - CRASH (the case Allrun/solver died on signal N; propagated as-is so
#                   the runner reports e.g. CRASH (SIGABRT) for a 134 abort)

set -u

caseDir=""
SKIP_RUN=false
RTOL="1e-4"
ATOL="1e-12"
COMPARE=""

while [ $# -gt 0 ]; do
    case "$1" in
    --no-run)
        SKIP_RUN=true
        shift
        ;;
    --rtol)
        RTOL="$2"
        shift 2
        ;;
    --atol)
        ATOL="$2"
        shift 2
        ;;
    --compare)
        COMPARE="$2"
        shift 2
        ;;
    -*)
        echo "Unknown option: $1" >&2
        exit 2
        ;;
    *)
        if [ -z "$caseDir" ]; then caseDir="$1"; else
            echo "Unexpected positional: $1" >&2
            exit 2
        fi
        shift
        ;;
    esac
done

if [ -z "$caseDir" ] || [ ! -d "$caseDir" ]; then
    echo "Case directory not found: $caseDir" >&2
    exit 2
fi

if [ -z "$COMPARE" ]; then
    COMPARE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/compareScalars.py"
fi

if [ ! -x "$COMPARE" ] && [ ! -f "$COMPARE" ]; then
    echo "Comparator not found: $COMPARE" >&2
    exit 2
fi

if [ "$SKIP_RUN" = false ]; then
    if [ ! -x "$caseDir/Allrun" ]; then
        echo "$caseDir/Allrun is missing or not executable" >&2
        exit 2
    fi

    # Clean prior run artefacts (preserve reference/). A swallowed clean can
    # leave a dirty slate that silently corrupts the comparison, so the clean
    # is checked and the slate is asserted afterwards.
    if [ -x "$caseDir/Allclean" ]; then
        if ! (cd "$caseDir" && ./Allclean) >/dev/null 2>&1; then
            echo "[runCase] Allclean failed in $caseDir" >&2
            exit 2
        fi
    fi
    if ls "$caseDir"/log.* >/dev/null 2>&1 || [ -d "$caseDir/postProcessing" ]; then
        echo "[runCase] dirty slate: log.*/postProcessing survived the clean in $caseDir" >&2
        exit 2
    fi

    echo "[runCase] Running $caseDir/Allrun"
    (cd "$caseDir" && ./Allrun)
    rc=$?
    # Propagate the case's own exit status so the runner can classify it: a
    # signal death (128+N) surfaces as CRASH, any other non-zero as the case's
    # reported failure. Infrastructure problems (a broken/absent clean, a dirty
    # slate, missing files) are the runner's own concern and exit 2 above.
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -gt 128 ]; then
            echo "[runCase] Allrun in $caseDir died on signal $(( rc - 128 )) (exit $rc)" >&2
        else
            echo "[runCase] Allrun in $caseDir exited non-zero ($rc)" >&2
        fi
        exit "$rc"
    fi
fi

if [ ! -d "$caseDir/postProcessing" ]; then
    echo "[runCase] No postProcessing/ produced under $caseDir" >&2
    exit 2
fi

if [ ! -d "$caseDir/reference/postProcessing" ]; then
    echo "[runCase] No reference baseline at $caseDir/reference/postProcessing" >&2
    exit 2
fi

python3 "$COMPARE" \
    --reference "$caseDir/reference/postProcessing" \
    --candidate "$caseDir/postProcessing" \
    --rtol "$RTOL" \
    --atol "$ATOL"
