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

# The palette (#287): the GUI keeps the user's, --png defaults to white, --palette picks one;
# ParaView's saved palette is never changed (#290).
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
assert sig(pgfViews.showGui) is None and sig(pgfViews.showCompareGui) is None, "GUI"
assert sig(pgfViews.writePngs) == sig(pgfViews.writeComparePngs) == "WhiteBackground", "png"
pgfViews._palette(None); assert simple.loaded == [], simple.loaded
'
printf '#!/bin/sh\necho "palette=$PGFVIEW_PALETTE"\n' >"$bin/paraview"
check "--palette NAME reaches the script" \
    bash -c '[ "$(PATH="$1:$PATH" "$2" view "$3" --palette BlackBackground)" = palette=BlackBackground ]' _ "$bin" "$PGF" "$TEST_TMP/case"
printf '#!/bin/sh\necho "pvbatch $*"\n' >"$bin/pvbatch" && chmod +x "$bin/pvbatch"
check "--png runs pvbatch --dr: the user's ParaView settings are not saved (#290)" \
    bash -c 'DISPLAY=:9 PATH="$1:$PATH" "$2" view "$3" --png "$4" | grep -q "^pvbatch --dr "' _ "$bin" "$PGF" "$TEST_TMP/case" "$TEST_TMP/png"

# GUI tabs (#290): one per view, named after it, replacing the startup tab and
# its empty RenderView1. A recording stub stands in for paraview.simple.
cat >"$stub/paraview/simple.py" <<'EOF2'
calls, layouts = [], {}
class View:
    def __init__(s, n): s.n = n
    def __repr__(s): return s.n
class Layout:
    def __init__(s, n): s.n, s.cells = n, {}; layouts[n] = s
    def SplitHorizontal(s, c, f): pass
    def AssignView(s, c, v): s.cells[c] = v
rv = View("RenderView1"); startup = Layout("Layout #1"); startup.AssignView(0, rv)
active = [rv]
def GetActiveView(): return active[0]
def SetActiveView(v): active[0] = v
def GetLayout(v=None): return next((l for l in layouts.values() if (v or active[0]) in l.cells.values()), None)
def GetViewsInLayout(l): return list(l.cells.values())
def CreateLayout(n): return Layout(n)
def RemoveLayout(l): calls.append(("RemoveLayout", l.n)); del layouts[l.n]
def Delete(p): calls.append(("Delete", p.n))
def LoadPalette(n): pass
EOF2
check "showGui: a tab per view named after it, the startup tab and RenderView1 gone" \
    env PYTHONPATH="$stub:$PGF_ROOT/applications/utilities/paraview" python3 -c '
import pgfViews as P
from paraview import simple as S
P.build = lambda case, time: ([("Ychar", S.View("Ychar")), ("T", S.View("T"))], 0.5)
P.showGui(type("C", (), {"label": "demo"})())
assert {n: [repr(v) for v in l.cells.values()] for n, l in S.layouts.items()} == \
    {"Ychar": ["Ychar"], "T": ["T"]}, S.layouts
assert ("RemoveLayout", "Layout #1") in S.calls and ("Delete", "RenderView1") in S.calls, S.calls
assert repr(S.GetActiveView()) == "Ychar"
'

check "--time: a written time is kept, another one warns and names the time shown" \
    env PYTHONPATH="$stub:$PGF_ROOT/applications/utilities/paraview" python3 -c '
import pgfViews as P
ts = [0.0, 0.01, 0.02]
assert P.writtenTime(ts, None) == (0.02, None)
assert P.writtenTime(ts, 0.01) == (0.01, None)
t, w = P.writtenTime(ts, 0.001)
assert t == 0.0 and "no results written at t = 0.001; rendering t = 0" in w and "0 and 0.01" in w, w
t, w = P.writtenTime(ts, 5)
assert t == 0.02 and w, w
'

# A - B without a Python-expression proxy (#290): its GUI editor segfaults
# ParaView 5.13. Real ParaView, no view (no display here).
if command -v pvpython >/dev/null; then
    cat >"$TEST_TMP/diff.py" <<EOF2
import sys
sys.path.insert(0, "$PGF_ROOT/applications/utilities/paraview")
import paraview.simple as pv, pgfViews
srcs = [pv.PointDatatoCellData(Input=pv.Wavelet(Maximum=m)) for m in (300.0, 255.0)]
d = pgfViews.diffSource(srcs[0], srcs[1], "RTData", "RTData A-B", 0.0)
lo, hi = d.CellData["RTData A-B"].GetRange()
assert d.GetXMLName() == "Calculator", d.GetXMLName()
assert 0 < lo <= hi <= 45 + 1e-6, (lo, hi)
names = [k[0] for k in pv.GetSources()]
assert "RTData A-B" in names and "RTData A-B inputs" in names, names
print("diff ok")
EOF2
    check "A - B is a Calculator on a named AppendAttributes, with the right values" \
        bash -c 'UCX_VFS_ENABLE=n timeout 120 pvpython "$1" 2>&1 | tail -3 | grep -qx "diff ok"' _ "$TEST_TMP/diff.py"
else
    echo "  SKIP pvpython not found: A - B proxy not checked"
fi

if command -v pvpython >/dev/null; then
    # GUI: --palette reads the palette's background without loading it; --png
    # loads it, under pvbatch --dr. Either way the settings file is left as it was.
    cat >"$TEST_TMP/palette.py" <<EOF2
import sys
sys.path.insert(0, "$PGF_ROOT/applications/utilities/paraview")
import paraview.simple as pv, pgfViews
if sys.argv[1] == "gui":
    bg = pgfViews.paletteBackground("BlackBackground")["Background"]
    assert [bg.GetElement(i) for i in range(3)] == [0, 0, 0]
else:
    pgfViews._palette("WhiteBackground")
    assert list(pv.GetSettingsProxy("ColorPalette").Foreground) == [0, 0, 0]
print("palette ok")
EOF2
    for path in gui png; do
        for start in none grey; do
            h="$TEST_TMP/home-$path-$start" && mkdir -p "$h/.config/ParaView"
            [ $start = grey ] && echo '{"settings":{"ColorPalette":{"BackgroundColor":[0.3,0.3,0.3]}}}' \
                >"$h/.config/ParaView/ParaView-UserSettings.json"
            dr=() && [ $path = png ] && dr=(--dr)
            check "$path: ParaView's saved palette is left as it was ($start)" bash -c '
                HOME="$1" XDG_CONFIG_HOME="$1/.config" UCX_VFS_ENABLE=n timeout 120 pvpython "${@:5}" "$2" "$3" 2>&1 |
                    grep -qx "palette ok" &&
                python3 -c "import json, os, sys
f = sys.argv[1]
s = json.load(open(f)).get(\"settings\", {}) if os.path.exists(f) else {}
want = {\"BackgroundColor\": [0.3] * 3} if sys.argv[2] == \"grey\" else None
assert s.get(\"ColorPalette\") == want, s
" "$1/.config/ParaView/ParaView-UserSettings.json" "$4"' _ "$h" "$TEST_TMP/palette.py" "$path" "$start" "${dr[@]}"
        done
    done
fi

[ "$TEST_FAILURES" -eq 0 ] || echo "$out"
finish
