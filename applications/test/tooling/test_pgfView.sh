#!/bin/bash
# pgfView GUI mode (#280): paraview --script runs pgfViewMain.py as <string>,
# without __file__, so it must find pgfViews through PGFVIEW_DIR. A fake
# paraview does the same; a stub paraview.simple stops once pgfViews loads.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

bin="$TEST_TMP/bin" stub="$TEST_TMP/stub"
mkdir -p "$bin" "$stub/paraview" "$TEST_TMP/case/system"
: >"$stub/paraview/__init__.py"
printf 'import sys\nprint("pgfViews imported")\nsys.exit(0)\n' >"$stub/paraview/simple.py"
cat >"$bin/paraview" <<'EOF'
#!/bin/sh
cd / && exec python3 -c 'import sys; exec(compile(open(sys.argv[1]).read(), "<string>", "exec"))' "${1#--script=}"
EOF
chmod +x "$bin/paraview"

out="$(cd / && PATH="$bin:$PATH" PYTHONPATH="$stub" \
    "$PGF_ROOT/applications/utilities/paraview/pgfView" "$TEST_TMP/case" 2>&1)"
rc=$?
check "GUI mode runs pgfViewMain.py as <string> and loads pgfViews (rc=$rc)" \
    bash -c '[ "$1" -eq 0 ] && grep -qx "pgfViews imported" <<<"$2"' _ "$rc" "$out"
check "  without a NameError on __file__" bash -c '! grep -q NameError <<<"$1"' _ "$out"
[ "$TEST_FAILURES" -eq 0 ] || echo "$out"
finish
