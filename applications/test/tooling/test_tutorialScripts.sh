#!/bin/bash
# Tutorial scripts run from any cwd (#257 PR E): every Allrun/Allclean changes
# into its own directory, CleanAllCases.sh passes from the repo root and from /
# (on a copy of tutorials/), and a DEM Allrun outside a --yade env stops with
# exit 2 and the pgf-env fix before creating anything.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

missing="$(find "$PGF_ROOT/tutorials" \( -name Allrun -o -name Allclean \) -type f -print0 |
    xargs -0 grep -L -e 'cd "${0%/\*}"' -e 'cd "$(dirname "${BASH_SOURCE\[0\]}")"')"
check "every Allrun/Allclean cds into its own directory${missing:+ (missing: $missing)}" [ -z "$missing" ]

copy="$TEST_TMP/copy"
mkdir -p "$copy/utilities"
cp -r "$PGF_ROOT/tutorials" "$copy/" && cp -r "$PGF_ROOT/utilities/bash_utils" "$copy/utilities/" ||
    die "cannot copy tutorials"
snapshot() { (cd "$copy/tutorials" && find . -print | LC_ALL=C sort | sha1sum); }
before="$(snapshot)"
(cd "$copy" && "$PGF_ENV" --bare -- tutorials/CleanAllCases.sh) >"$TEST_TMP/root.log" 2>&1
rc=$?
check "CleanAllCases.sh from the repo root (rc=$rc)" [ "$rc" -eq 0 ]
(cd / && "$PGF_ENV" --bare -- "$copy/tutorials/CleanAllCases.sh") >"$TEST_TMP/slash.log" 2>&1
rc=$?
check "CleanAllCases.sh from / (rc=$rc)" [ "$rc" -eq 0 ]
check "cleaning a clean copy changes nothing" [ "$before" = "$(snapshot)" ]

dem="$copy/tutorials/cases/MicroTGA-DEM_lambdaDotOff"
(cd / && env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" "$dem/Allrun") >/dev/null 2>"$TEST_TMP/guard.err"
rc=$?
check "a DEM Allrun without the solver exits 2 (rc=$rc)" [ "$rc" -eq 2 ]
check "  and prints the pgf-env run fix" grep -q "run this via: pgf-env run $dem" "$TEST_TMP/guard.err"
check "  before creating anything" bash -c '[ ! -e "$1/0" ] && ! ls "$1"/log.* >/dev/null 2>&1' _ "$dem"

[ "$TEST_FAILURES" -eq 0 ] || { tail -20 "$TEST_TMP/slash.log"; cat "$TEST_TMP/guard.err"; }
finish
