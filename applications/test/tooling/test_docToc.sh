#!/bin/bash
# doc/README.md, the table of contents, lists each page's current sections:
# utilities/gen-doc-toc.py regenerates them. Fake bashrc; nothing is built.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

check "doc/README.md section links match the pages (utilities/gen-doc-toc.py regenerates them)" \
    python3 "$PGF_ROOT/utilities/gen-doc-toc.py" --check
finish
