"""
pgfViews -- scripted ParaView views for porousGasificationFoam cases.

Run under pvpython / pvbatch / the ParaView GUI (see pgfView next to this
file). Every builder takes a Case and its options from the case's
system/paraviewViews.py and returns a list of (title, view) pairs.

The case decides *what* to show (system/paraviewViews.py); this module
decides *how*. Field names come from the case (solidComponents, the arrays
the reader finds), so a renamed or removed specie needs no edit here.
"""

import glob
import json
import os
import re
import runpy
import sys
from pathlib import Path

import paraview.simple as pv

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "..", "test", "regression", "tools"))
from compareScalars import collect_dat_files, parse_dat  # noqa: E402

# Default mesh fields after the solid species, shown when present.
MESH_FIELDS = ["porosityF", "rhos", "Ts", "T", "lambda", "lambdaDot"]

# SolidState::Regime (solidState.H), in order of rising solid fraction.
SOLID_REGIMES = ["empty", "trace", "dispersed", "connected"]

# Chart line colours (Okabe-Ito), cycled per series.
SERIES_COLORS = [(0.0, 0.45, 0.7), (0.84, 0.37, 0.0), (0.0, 0.62, 0.45),
                 (0.8, 0.47, 0.65), (0.9, 0.62, 0.0), (0.34, 0.71, 0.91)]

VIEW_FILE = os.path.join("system", "paraviewViews.py")
DEFAULT_VIEWS = [("meshFields", {}), ("solidRegime", {}), ("overTime", {})]


# --------------------------------------------------------------------------
# Case
# --------------------------------------------------------------------------

def _readDictList(path, key):
    """Return the word list `key ( a b c );` from an OpenFOAM dict, or []."""
    try:
        with open(path) as f:
            text = f.read()
    except OSError:
        return []
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"\b" + key + r"\s*\(([^)]*)\)", text)
    return m.group(1).split() if m else []


def _readDictScalar(path, key):
    try:
        with open(path) as f:
            m = re.search(r"^\s*" + key + r"\s+([^;\s]+)\s*;", f.read(), re.M)
    except OSError:
        return None
    return float(m.group(1)) if m else None


def _timeDirs(path):
    out = []
    for d in os.listdir(path):
        try:
            out.append(float(d))
        except ValueError:
            pass
    return out


class Case:
    """A case directory (source tutorial or a run copy) and its readers."""

    def __init__(self, path):
        self.path = os.path.abspath(path)
        if not os.path.isdir(os.path.join(self.path, "system")):
            raise SystemExit("pgfView: not an OpenFOAM case: " + self.path)
        self.name = os.path.basename(self.path)
        self._mesh = None

    def file(self, *parts):
        return os.path.join(self.path, *parts)

    def solidComponents(self):
        return _readDictList(
            self.file("constant", "solidThermophysicalProperties"),
            "solidComponents",
        )

    def decomposed(self):
        """True when processor0 holds more time directories than the root."""
        proc = self.file("processor0")
        if not os.path.isdir(proc):
            return False
        return len(_timeDirs(proc)) > len(_timeDirs(self.path))

    def mesh(self):
        """OpenFOAM reader on <case>/<name>.foam (touched on demand)."""
        if self._mesh is None:
            foam = self.file(self.name + ".foam")
            if not os.path.exists(foam):
                open(foam, "w").close()
            r = pv.OpenFOAMReader(registrationName=self.name, FileName=foam)
            r.CaseType = "Decomposed Case" if self.decomposed() else "Reconstructed Case"
            r.MeshRegions = ["internalMesh"]
            r.UpdatePipelineInformation()
            r.CellArrays = list(r.CellArrays.Available)
            r.UpdatePipeline((r.TimestepValues or [0.0])[-1])  # bounds for slices
            self._mesh = r
        return self._mesh

    def cellArrays(self):
        return list(self.mesh().CellArrays.Available)

    def times(self):
        return list(self.mesh().TimestepValues or [0.0])

    def deltaT(self):
        return _readDictScalar(self.file("system", "controlDict"), "deltaT")

    def demSeries(self, subdir, pattern):
        """A .series file for YADE frames <stem>_<iter>, timed iter*deltaT.

        Gives the DEM frames the mesh's time axis; returns None if absent.
        """
        files = glob.glob(self.file(subdir, pattern))
        if not files:
            return None
        dt = self.deltaT() or 1.0
        frames = []
        for f in files:
            m = re.search(r"_(\d+)\.\w+$", f)
            if m:
                frames.append((int(m.group(1)) * dt, os.path.basename(f)))
        frames.sort()
        series = self.file(subdir, pattern.replace("*", "") + ".series")
        with open(series, "w") as out:
            json.dump({"file-series-version": "1.0",
                       "files": [{"name": n, "time": t} for t, n in frames]},
                      out, indent=1)
        return series

    def viewSpec(self):
        """The (name, options) list from system/paraviewViews.py."""
        path = self.file(VIEW_FILE)
        if not os.path.exists(path):
            return DEFAULT_VIEWS
        spec = runpy.run_path(path).get("views")
        if spec is None:
            raise SystemExit("pgfView: %s defines no `views` list" % path)
        return [(v, {}) if isinstance(v, str) else (v[0], dict(v[1])) for v in spec]


# --------------------------------------------------------------------------
# Building blocks
# --------------------------------------------------------------------------

def _renderView(title):
    v = pv.CreateRenderView(registrationName=title)
    v.OrientationAxesVisibility = 1
    v.CameraParallelProjection = 1
    return v


def _color(rep, view, assoc, field, opts):
    pv.ColorBy(rep, (assoc, field))
    lut = pv.GetColorTransferFunction(field)
    rng = opts.get("ranges", {}).get(field)
    if rng:
        lut.RescaleTransferFunction(*rng)
        lut.AutomaticRescaleRangeMode = "Never"
    else:
        rep.RescaleTransferFunctionToDataRange(False, True)
    rep.SetScalarBarVisibility(view, True)
    bar = pv.GetScalarBar(lut, view)
    bar.Title, bar.ComponentTitle = field, ""
    bar.WindowLocation = "Any Location"
    bar.Position, bar.ScalarBarLength = [0.89, 0.1], 0.8
    bar.TitleFontSize = bar.LabelFontSize = 14
    bar.RangeLabelFormat = "%-#.6g"
    return lut


def _sliced(case, opts):
    """The mesh, or a slice of it when opts['slice'] = {normal, origin}."""
    sl = opts.get("slice")
    if not sl:
        return case.mesh()
    s = pv.Slice(Input=case.mesh())
    s.SliceType = "Plane"
    b = case.mesh().GetDataInformation().GetBounds()
    n = sl.get("normal", [0, 0, 1])
    # Nudge the default centre off a cell-face plane, where slices break up.
    s.SliceType.Origin = sl.get("origin", [
        (b[2 * k] + b[2 * k + 1]) / 2 + 1e-3 * n[k] * (b[2 * k + 1] - b[2 * k]) for k in range(3)
    ])
    s.SliceType.Normal = n
    return s


def _camera(view, opts, fallback="+z"):
    """Look from the `camera` side ("+z": camera on +z, looking down -z)."""
    axis = opts.get("camera", fallback)
    toward = "Negative" if axis[0] == "+" else "Positive"
    getattr(view, "ResetActiveCameraTo" + toward + axis[-1].upper())()
    view.ResetCamera(False)


def _unlit(rep):
    """Exact colour-map colours on flat surfaces (no shading)."""
    rep.Ambient, rep.Diffuse = 1.0, 0.0


def _outline(case, view):
    rep = pv.Show(case.mesh(), view)
    rep.SetRepresentationType("Outline")
    rep.AmbientColor = rep.DiffuseColor = [0.2, 0.2, 0.2]
    rep.ColorArrayName = ["POINTS", ""]


# --------------------------------------------------------------------------
# Views
# --------------------------------------------------------------------------

def meshFields(case, opts):
    """One view per field: Y<specie> from solidComponents, then MESH_FIELDS."""
    have = set(case.cellArrays())
    fields = opts.get("fields") or (
        ["Y" + s for s in case.solidComponents()] + MESH_FIELDS
    )
    src = _sliced(case, opts)
    out = []
    for f in fields:
        if f not in have:
            continue
        v = _renderView(f)
        rep = pv.Show(src, v)
        _color(rep, v, "CELLS", f, opts)
        _unlit(rep)
        if opts.get("slice"):
            _outline(case, v)
        _camera(v, opts, _viewAxis(case, opts))
        out.append((f, v))
    return out


def _viewAxis(case, opts):
    """Look along the slice normal, else the mesh's thinnest axis (2D cases).

    The side is the one that keeps the remaining axes right-handed.
    """
    sl = opts.get("slice")
    if sl:
        n = sl.get("normal", [0, 0, 1])
        k = max(range(3), key=lambda i: abs(n[i]))
    else:
        b = case.mesh().GetDataInformation().GetBounds()
        k = min((2, 1, 0), key=lambda i: b[2 * i + 1] - b[2 * i])
    return ("+x", "-y", "+z")[k]


def solidRegime(case, opts):
    """solidRegime with fixed categories (SolidState::Regime)."""
    if "solidRegime" not in case.cellArrays():
        return []
    v = _renderView("solidRegime")
    rep = pv.Show(_sliced(case, opts), v)
    _unlit(rep)
    lut = _color(rep, v, "CELLS", "solidRegime", {})
    lut.InterpretValuesAsCategories = 1
    lut.Annotations = sum(([str(i), n] for i, n in enumerate(SOLID_REGIMES)), [])
    lut.IndexedColors = [0.85, 0.85, 0.85, 0.99, 0.75, 0.30, 0.35, 0.65, 0.85, 0.40, 0.20, 0.10]
    if opts.get("slice"):
        _outline(case, v)
    _camera(v, opts, _viewAxis(case, opts))
    return [("solidRegime", v)]


def porosityThreshold(case, opts):
    """Cells with porosityF below opts['below'] (default 0.999): the solid."""
    t = pv.Threshold(Input=case.mesh())
    t.Scalars = ["CELLS", "porosityF"]
    t.LowerThreshold, t.UpperThreshold = 0.0, opts.get("below", 0.999)
    v = _renderView("porosityThreshold")
    rep = pv.Show(t, v)
    _color(rep, v, "CELLS", "porosityF", opts)
    _outline(case, v)
    _camera(v, opts, _viewAxis(case, {}))
    return [("porosityThreshold", v)]


def _spheres(case):
    """YADE VTKRecorder frames, else a spheres/spheres.pvd written by the case."""
    series = case.demSeries("spheres", "vtk-spheres_*.pvtu")
    if series is None and os.path.exists(case.file("spheres", "spheres.pvd")):
        series = case.file("spheres", "spheres.pvd")
    if not series:
        return None
    src = pv.OpenDataFile(series)
    src.UpdatePipeline()
    return src


def _radiusArray(src):
    have = src.PointData.keys()
    return next((a for a in ("radii", "radius") if a in have), None)


def _sphereGlyphs(src):
    g = pv.Glyph(Input=src, GlyphType="Sphere")
    g.ScaleArray = ["POINTS", _radiusArray(src)]
    g.ScaleFactor = 2.0  # unit glyph has radius 0.5
    g.GlyphMode = "All Points"
    g.GlyphType.ThetaResolution = g.GlyphType.PhiResolution = 16
    return g


def demSpheres(case, opts):
    """YADE spheres as glyphs scaled by radii, coloured by opts['colorBy']."""
    src = _spheres(case)
    if src is None:
        return []
    g = _sphereGlyphs(src)
    have = src.PointData.keys()
    out = []
    for f in opts.get("colorBy", [_radiusArray(src), "lambda"]):
        if f not in have:
            continue
        v = _renderView("spheres " + f)
        rep = pv.Show(g, v)
        _color(rep, v, "POINTS", f, opts)
        _outline(case, v)
        _camera(v, opts, "-y")
        out.append(("spheres_" + f, v))
    return out


def demSprings(case, opts):
    """Springs (springs/springs.pvd) by springType over translucent spheres."""
    pvd = case.file("springs", "springs.pvd")
    if not os.path.exists(pvd):
        return []
    v = _renderView("springs")
    springs = pv.PVDReader(FileName=pvd)
    rep = pv.Show(springs, v)
    rep.LineWidth = 2.0
    lo, hi = springs.CellData["springType"].GetRange() if "springType" in springs.CellData.keys() else (0, 0)
    if hi - lo >= 1:  # springType holds small integers; skip a degenerate range
        _color(rep, v, "CELLS", "springType", opts)
    else:
        rep.ColorArrayName = ["CELLS", ""]
        rep.AmbientColor = rep.DiffuseColor = [0.0, 0.45, 0.7]
    src = _spheres(case)
    if src is not None:
        s = pv.Show(_sphereGlyphs(src), v)
        s.ColorArrayName = ["POINTS", ""]
        s.Opacity = 0.25
    _outline(case, v)
    _camera(v, opts, "-y")
    return [("springs", v)]


def _table(path):
    """postProcessing .dat -> vtkTable, parsed by compareScalars.parse_dat."""
    from vtkmodules.vtkCommonCore import vtkDoubleArray
    from vtkmodules.vtkCommonDataModel import vtkTable
    cols, rows = parse_dat(path)
    width = len(rows[0]) if rows else 0
    names = cols if len(cols) == width else ["col%d" % i for i in range(width)]
    t = vtkTable()
    for i, n in enumerate(names):
        a = vtkDoubleArray()
        a.SetName(n)
        for r in rows:
            a.InsertNextValue(r[i])
        t.AddColumn(a)
    return t, names


def overTime(case, opts):
    """postProcessing/<function>/*.dat plotted against time.

    opts['groups'] = {title: [function, ...]} puts several in one chart;
    otherwise one chart per function. opts['skip'] lists functions to omit.
    """
    root = case.file("postProcessing")
    if not os.path.isdir(root):
        return []
    byFn = {}
    for p in sorted(collect_dat_files(Path(root))):
        fn = os.path.relpath(str(p), root).split(os.sep)[0]
        byFn.setdefault(fn, []).append(p)
    skip = set(opts.get("skip", ["residuals"]))
    groups = opts.get("groups") or {fn: [fn] for fn in byFn if fn not in skip}
    out = []
    for title, fns in groups.items():
        if not any(fn in byFn for fn in fns):
            continue
        v = pv.CreateView("XYChartView", registrationName="overTime " + title)
        v.ChartTitle = title
        v.BottomAxisTitle = "Time [s]"
        v.LeftAxisLabelNotation, v.LeftAxisLabelPrecision = "Mixed", 6
        colors = iter(SERIES_COLORS * 4)
        for fn in fns:
            for p in byFn.get(fn, []):
                table, names = _table(p)
                src = pv.PVTrivialProducer(registrationName=fn)
                src.GetClientSideObject().SetOutput(table)
                rep = pv.Show(src, v, "XYChartRepresentation")
                rep.AttributeType = "Row Data"
                rep.UseIndexForXAxis = 0
                rep.XArrayName = names[0]
                rep.SeriesVisibility = names[1:]
                rep.SeriesLabel = sum(([n, fn if len(names) == 2 else n] for n in names[1:]), [])
                rep.SeriesColor = sum(([n] + [str(c) for c in next(colors)] for n in names[1:]), [])
                rep.SeriesLineThickness = sum(([n, "2"] for n in names[1:]), [])
        out.append(("overTime_" + title, v))
    return out


VIEWS = {f.__name__: f for f in
         (meshFields, solidRegime, porosityThreshold, demSpheres, demSprings, overTime)}


# --------------------------------------------------------------------------
# Entry points
# --------------------------------------------------------------------------

def build(case, time=None):
    """Build the case's views at `time` (default: last mesh time)."""
    pv.LoadPalette("WhiteBackground")
    # Set the time first: colour ranges are fitted to the data shown at build.
    t = case.times()[-1] if time is None else time
    scene = pv.GetAnimationScene()
    scene.UpdateAnimationUsingDataTimeSteps()
    scene.AnimationTime = t
    out = []
    for name, opts in case.viewSpec():
        if name not in VIEWS:
            raise SystemExit("pgfView: unknown view %r (have: %s)" % (name, ", ".join(VIEWS)))
        out += VIEWS[name](case, opts)
    if not out:
        raise SystemExit("pgfView: no views apply to " + case.path)
    return out


def showGui(case):
    """One layout tab per view, the time bar on the last mesh time."""
    built = build(case)
    first = pv.GetLayout()
    for i, (title, v) in enumerate(built):
        layout = first if i == 0 else pv.CreateLayout(title)
        if i == 0:
            pv.RenameLayout(title, first)
        pv.AssignViewToLayout(view=v, layout=layout, hint=0)
    pv.SetActiveView(built[0][1])


def writePngs(case, outDir, time=None, size=(1000, 750)):
    """Render every view at `time` (default: last mesh time) to outDir/*.png."""
    os.makedirs(outDir, exist_ok=True)
    t = case.times()[-1] if time is None else time
    written = []
    for title, v in build(case, t):
        v.ViewTime = t
        path = os.path.join(outDir, re.sub(r"[^\w.-]+", "_", title) + ".png")
        pv.SaveScreenshot(path, v, ImageResolution=list(size), FontScaling="Do not scale fonts")
        written.append(path)
        print("pgfView: wrote " + path)
    return written
