#!/bin/bash
# Tutorial scripts run from any cwd (#257 PR E): every Allrun/Allclean changes
# into its own directory, CleanAllCases.sh passes from the repo root and from /
# (on a copy of tutorials/), and a DEM Allrun outside a --yade env stops with
# exit 2 and the pgf run fix before creating anything.

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
(cd "$copy" && "$PGF" exec --bare -- tutorials/CleanAllCases.sh) >"$TEST_TMP/root.log" 2>&1
rc=$?
check "CleanAllCases.sh from the repo root (rc=$rc)" [ "$rc" -eq 0 ]
(cd / && "$PGF" exec --bare -- "$copy/tutorials/CleanAllCases.sh") >"$TEST_TMP/slash.log" 2>&1
rc=$?
check "CleanAllCases.sh from / (rc=$rc)" [ "$rc" -eq 0 ]
check "cleaning a clean copy changes nothing" [ "$before" = "$(snapshot)" ]

dem="$copy/tutorials/cases/MicroTGA-DEM_lambdaDotOff"
(cd / && env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" "$dem/Allrun") >/dev/null 2>"$TEST_TMP/guard.err"
rc=$?
check "a DEM Allrun without the solver exits 2 (rc=$rc)" [ "$rc" -eq 2 ]
check "  and prints the pgf run fix" grep -q "run this via: pgf run $dem" "$TEST_TMP/guard.err"
check "  before creating anything" bash -c '[ ! -e "$1/0" ] && ! ls "$1"/log.* >/dev/null 2>&1' _ "$dem"

# A plain cp -rL of a DEM case outside the checkout (#280): its scripts and
# shared helpers resolve through PGF_ROOT, which pgf exports.
alone="$TEST_TMP/alone/lambdaDotOff"
mkdir -p "${alone%/*}" && cp -rL "$PGF_ROOT/tutorials/cases/MicroTGA-DEM_lambdaDotOff" "$alone" || die "cannot copy"
(cd / && env -i PATH=/usr/bin:/bin HOME="$TEST_TMP" PGF_ROOT="$PGF_ROOT" "$alone/Allrun") >/dev/null 2>"$TEST_TMP/alone.err"
rc=$?
check "a copied DEM Allrun finds demGuard.sh through PGF_ROOT (rc=$rc)" \
    bash -c '[ "$1" -eq 2 ] && grep -q "run this via: pgf run" "$2"' _ "$rc" "$TEST_TMP/alone.err"
bad=""
for f in $(grep -rl 'PGF_ROOT:-' "$PGF_ROOT/tutorials" --include=Allrun); do
    for rel in $(grep -o '${PGF_ROOT:-[^}]*}/[^" ]*' "$f" | sed 's/^${PGF_ROOT:-[^}]*}\///'); do
        [ -e "$PGF_ROOT/$rel" ] || bad="$bad ${f#"$PGF_ROOT"/}:$rel"
    done
done
check "every \${PGF_ROOT:-...}/path in an Allrun exists${bad:+ (missing:$bad)}" [ -z "$bad" ]
missing="$(grep -rlE '(\.\./)+(DEM_common/|applications/test/regression/tools/)' "$PGF_ROOT/tutorials" \
    --include=Allrun | xargs -r grep -LE 'PGF_ROOT')"
check "no Allrun reaches DEM_common or regression tools by a bare relative path${missing:+ ($missing)}" [ -z "$missing" ]

[ "$TEST_FAILURES" -eq 0 ] || { tail -20 "$TEST_TMP/slash.log"; cat "$TEST_TMP/guard.err"; }
finish
