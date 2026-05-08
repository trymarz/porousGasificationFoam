#!/bin/bash
# Run one regression case and compare its postProcessing/ output against the
# committed reference/postProcessing/ baseline.
#
# Usage:
#   runCase.sh <caseDir> [--no-run] [--rtol R] [--atol A] [--compare PATH]
#
# Exit codes:
#   0  - PASS (within tolerance)
#   1  - FAIL (numerical divergence)
#   2  - infrastructure error (missing files, run failure, etc.)

set -u

caseDir=""
SKIP_RUN=false
RTOL="1e-4"
ATOL="1e-12"
COMPARE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-run) SKIP_RUN=true; shift ;;
        --rtol)   RTOL="$2"; shift 2 ;;
        --atol)   ATOL="$2"; shift 2 ;;
        --compare) COMPARE="$2"; shift 2 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)  if [ -z "$caseDir" ]; then caseDir="$1"; else
                echo "Unexpected positional: $1" >&2; exit 2; fi
            shift ;;
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

    # Clean prior run artefacts (preserve reference/).
    if [ -x "$caseDir/Allclean" ]; then
        ( cd "$caseDir" && ./Allclean ) >/dev/null 2>&1 || true
    fi

    echo "[runCase] Running $caseDir/Allrun"
    if ! ( cd "$caseDir" && ./Allrun ); then
        echo "[runCase] Allrun failed in $caseDir" >&2
        exit 2
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
