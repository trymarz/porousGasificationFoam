#!/bin/bash
# Recapture a regression baseline from a finished run.
#
# Usage:
#   captureBaseline.sh <caseDir> [--run-dir D] [--all-files] [--dry-run]
#                      [--rtol R] [--atol A]
#
# The candidate is D/postProcessing, where D is the run copy runCase.sh / Allrun
# printed ("run copy ..."), or <caseDir>/postProcessing without --run-dir. By
# default only files that already exist in <caseDir>/reference/postProcessing
# AND fail the comparison are copied, so files that agree within tolerance are
# not churned; --all-files copies every reference file. New output files are
# never added: register a new metric by copying it by hand. For each copied file
# a before/after table (first, middle and last sample) is printed. --dry-run
# prints the table and copies nothing. Commit the result yourself, with the
# reason the baseline moved.
#
# Exit: 0 done (or nothing to copy), 2 usage or infrastructure error.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPARE="$SCRIPT_DIR/compareScalars.py"

caseDir=""
runDir=""
allFiles=false
dryRun=false
RTOL="1e-4"
ATOL="1e-12"

while [ $# -gt 0 ]; do
    case "$1" in
    --run-dir) runDir="$2"; shift 2 ;;
    --all-files) allFiles=true; shift ;;
    --dry-run) dryRun=true; shift ;;
    --rtol) RTOL="$2"; shift 2 ;;
    --atol) ATOL="$2"; shift 2 ;;
    -h | --help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)
        [ -z "$caseDir" ] || { echo "Unexpected positional: $1" >&2; exit 2; }
        caseDir="$1"
        shift
        ;;
    esac
done

[ -n "$caseDir" ] && [ -d "$caseDir" ] || { echo "Case directory not found: ${caseDir:-<none>}" >&2; exit 2; }
caseDir="$(cd "$caseDir" && pwd)"
refDir="$caseDir/reference/postProcessing"
candDir="${runDir:-$caseDir}/postProcessing"
[ -d "$refDir" ] || { echo "No baseline at $refDir" >&2; exit 2; }
[ -d "$candDir" ] || { echo "No run output at $candDir" >&2; exit 2; }
[ "$(cd "$candDir" && pwd)" != "$(cd "$refDir" && pwd)" ] || { echo "Candidate and reference are the same directory" >&2; exit 2; }

# compareScalars.py prints one "OK|FAIL|MISS  <rel>" line per reference file.
report="$(python3 "$COMPARE" --reference "$refDir" --candidate "$candDir" \
    --rtol "$RTOL" --atol "$ATOL" 2>&1)"
rc=$?
[ "$rc" -le 1 ] || { printf '%s\n' "$report" >&2; echo "comparator error (exit $rc)" >&2; exit 2; }

if [ "$allFiles" = true ]; then
    pattern='^(OK|FAIL)  '
else
    pattern='^FAIL  '
fi
mapfile -t files < <(printf '%s\n' "$report" | grep -E "$pattern" | sed -E 's/^[A-Z]+  //')
missing="$(printf '%s\n' "$report" | grep -c '^MISS  ')"

echo "captureBaseline: $caseDir"
echo "  candidate: $candDir"
[ "$missing" -gt 0 ] && echo "  $missing reference file(s) missing from the run; left untouched"
if [ "${#files[@]}" -eq 0 ]; then
    echo "  nothing to copy: every reference file is within tolerance (rtol=$RTOL, atol=$ATOL)"
    exit 0
fi

python3 - "$refDir" "$candDir" "${files[@]}" <<'PY'
import sys
from pathlib import Path

def rows(path):
    out = []
    for line in open(path):
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        try:
            out.append([float(x) for x in t])
        except ValueError:
            pass
    return out

ref_root, cand_root, files = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
for rel in files:
    before, after = rows(ref_root / rel), rows(cand_root / rel)
    print(f"\n  {rel}  ({len(before)} -> {len(after)} rows)")
    print(f"    {'sample':<7} {'col':>3} {'time':>13} {'before':>15} {'after':>15} {'rel diff':>10}")
    n = min(len(before), len(after))
    for label, i in (("first", 0), ("middle", n // 2), ("last", n - 1)):
        if n == 0:
            break
        b, a = before[i], after[i]
        for j in range(1, min(len(b), len(a))):
            d = abs(a[j] - b[j]) / max(abs(a[j]), abs(b[j]), 1e-300)
            print(f"    {label:<7} {j:>3} {b[0]:>13.6g} {b[j]:>15.8e} {a[j]:>15.8e} {d:>10.2e}")
PY

echo
if [ "$dryRun" = true ]; then
    echo "  --dry-run: ${#files[@]} file(s) would be copied; nothing changed"
    exit 0
fi
for rel in "${files[@]}"; do
    cp "$candDir/$rel" "$refDir/$rel" || exit 2
    echo "  copied $rel"
done
echo "  ${#files[@]} file(s) updated under $refDir; review with git diff, then commit with the reason"
