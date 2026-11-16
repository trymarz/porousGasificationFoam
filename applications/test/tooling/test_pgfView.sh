#!/bin/bash
# pgfView GUI mode (#280, #287): paraview --script runs pgfViewMain.py as <string>,
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
out2="$(cd / && PATH="$bin:$PATH" PYTHONPATH="$stub" env -u DISPLAY "$PGF" view "$TEST_TMP/case" 2>&1)"
check "pgf view runs pgfView with the caller's ParaView" grep -qx "pgfViews imported" <<<"$out2"

# The palette (#287): the GUI keeps the user's, --png defaults to white, --palette picks one.
cat >"$stub/paraview/simple.py" <<'EOF2'
loaded = []
def LoadPalette(name):
    loaded.append(name)
EOF2
check "the GUI builders leave the palette alone; PNGs default to WhiteBackground" \
    env PYTHONPATH="$stub:$PGF_ROOT/applications/utilities/paraview" python3 -c '
import inspect, pgfViews
from paraview import simple
sig = lambda f: inspect.signature(f).parameters["palette"].default
assert sig(pgfViews.build) is None and sig(pgfViews.showGui) is None, "GUI"
assert sig(pgfViews.buildCompare) is None and sig(pgfViews.showCompareGui) is None, "compare GUI"
assert sig(pgfViews.writePngs) == sig(pgfViews.writeComparePngs) == "WhiteBackground", "png"
pgfViews._palette(None); assert simple.loaded == [], simple.loaded
pgfViews._palette("BlackBackground"); assert simple.loaded == ["BlackBackground"], simple.loaded
'
printf '#!/bin/sh\necho "palette=$PGFVIEW_PALETTE"\n' >"$bin/paraview"
check "--palette NAME reaches the script" \
    bash -c '[ "$(PATH="$1:$PATH" "$2" view "$3" --palette BlackBackground)" = palette=BlackBackground ]' _ "$bin" "$PGF" "$TEST_TMP/case"

[ "$TEST_FAILURES" -eq 0 ] || echo "$out"
finish
