# Viewing a case in ParaView

`applications/utilities/paraview/pgfView` opens a case in ParaView with its views already set up, or renders the same views to PNGs without a screen. The case says which views it wants in `system/paraviewViews.py`; the library `pgfViews.py` next to `pgfView` builds them.

## Use it

```bash
P=applications/utilities/paraview/pgfView
$P tutorials/cases/solidAdvection                 # GUI, one layout tab per view
$P <runDir>/MicroTGA-DEM_lambdaDotOff --png out/  # headless: out/<view>.png
$P <case> --png out/ --time 0.001 --size 1600x1200
```

`<case>` is a tutorial or a finished run copy of one (for example a `--keep-runs` copy from the regression runner). Decomposed cases are read as decomposed when `processor0` has more time directories than the case root. A `<case>.foam` file is created on first use (`*.foam` is gitignored).

`--png` renders the last mesh time unless `--time` is given; each view becomes one PNG named after it. It also works inside `./pgf exec`.

## Compare two cases

```bash
$P <runDir>/pyrolysis --compare tutorials/cases/canonical/pyrolysis/reference --png out/
$P <runDir>/pyrolysis --compare <runDir>/pyrolysisE           # GUI, panes side by side
```

`--compare <other>` shows the first case's views for both cases. `<other>` is a second case or run copy, or a `postProcessing`-only directory such as `<case>/reference`.

- `overTime`: each chart overlays both cases, the first solid and `<other>` dashed in the same colour, labelled `<function> (<case name>)`.
- Mesh and DEM views, when `<other>` has a mesh: panes `A | B` with linked cameras and one shared colour range. `meshFields` adds a third pane `A - B` when both meshes have the same number of cells: the cell-by-cell difference on a colour map symmetric about 0 (the magnitude of the difference for vectors).
- Against a `reference/` there are no fields, so only the charts are shown.

The time is the last one both cases have, unless `--time` is given. With `--png` each view is one PNG with its panes side by side, each `--size` large; the GUI opens one layout tab per view.

## Requirements

- ParaView with Python: on Debian `apt install paraview python3-paraview` (5.13 is what this was written against; it uses `paraview.simple` only).
- Headless `--png`: Debian's ParaView renders through X only (no EGL or OSMesa), so without a display `pgfView` runs `pvbatch` under `xvfb-run` (`apt install xvfb`). Pass `--xvfb` to force that when `DISPLAY` is set but unusable (for example in a sandbox or over SSH).

## The views

| View | Shows | Options |
|---|---|---|
| `meshFields` | One view per field: `Y<specie>` for each entry of `solidComponents` in `constant/solidThermophysicalProperties`, then `porosityF`, `rhos`, `Ts`, `T`, `lambda`, `lambdaDot`. Fields the case does not write are skipped. | `fields`, `slice`, `ranges`, `camera` |
| `solidRegime` | `solidRegime` with fixed categories: empty, trace, dispersed, connected (`SolidState::Regime`). | `slice`, `camera` |
| `porosityThreshold` | Cells with `porosityF` below `below` (default 0.999), i.e. where solid is left. | `below`, `ranges`, `camera` |
| `demSpheres` | YADE spheres (`spheres/vtk-spheres_<iter>.pvtu`, else `spheres/spheres.pvd`) as sphere glyphs scaled by `radii` (or `radius`), one view per colour field. Fields the spheres do not carry are skipped. | `colorBy` (default `["radii", "lambda"]`), `ranges`, `camera` |
| `demSprings` | Springs (`springs/springs.pvd`) coloured by `springType`, over translucent spheres. | `ranges`, `camera` |
| `overTime` | `postProcessing/<function>/*/*.dat` against time, parsed with the regression runner's own `compareScalars.parse_dat`. | `groups`, `skip` (default `["residuals"]`) |

Options:

- `slice`: `{"normal": [0, 1, 0], "origin": [x, y, z]}`; the origin defaults to the mesh centre. Without `slice` the whole mesh surface is shown (right for 2D cases).
- `ranges`: `{"porosityF": [0, 1]}` fixes a colour range; otherwise it fits the data.
- `camera`: `"+x"`, `"-y"`, … the side to look from (`"+z"` puts the camera on +z). The default looks along the slice normal (`+x`, `-y` or `+z`, so the remaining axes point right and up); without a slice it looks along the mesh's thinnest axis, so a 2D case is seen face on.
- `groups`: `{"chart title": ["functionA", "functionB"]}` puts several function objects in one chart; without it each function gets its own chart. With `groups`, functions not listed are not shown, and a group none of whose functions the case writes is left out, so a family can share one list.

YADE names sphere frames by iteration. `pgfView` writes `spheres/vtk-spheres_.series` that times frame `<iter>` at `iter * deltaT` (from `system/controlDict`), so spheres and mesh share the time bar.

## Add views to a case

Create `system/paraviewViews.py` with a `views` list; each entry is a view name or `(name, options)`:

```python
views = [
    ("meshFields", {"slice": {"normal": [0, 1, 0]}}),
    "solidRegime",
    ("overTime", {"groups": {"Ts": ["TsMin", "TsAverage", "TsMax"]}}),
]
```

A case without the file gets `meshFields`, `solidRegime` and `overTime`. Do not list species or field names that come from the case: `meshFields` reads `solidComponents`, so renaming or removing a specie needs no edit to the view file.

A family of cases shares one file the same way it shares its dicts: the MicroTGA-DEM lambdaDot fixtures symlink `system/paraviewViews.py` from `MicroTGA-DEM_lambdaDot_common`, and `charOnlyMoveCases`, `canonical`, `canonical/conservation` and `prescribedUsCases` keep one `paraviewViews.py` in the family directory that each case links as `system/paraviewViews.py` (the regression runner's run copy links the family directory's files too). Copy such a case with `cp -rL`, which copies the linked files instead of links that dangle outside the family.

To add a new kind of view, write a function `(case, opts) -> [(title, view), ...]` in `pgfViews.py` and add it to `VIEWS`. `--compare` calls it once per case, so it works there too; pass `opts` on to `_color` so the second case grows the first one's colour range.

## Relation to foamcli

foamcli's `render` and `gallery` make 2D matplotlib slices with no ParaView. `pgfView` is the ParaView side: 3D, DEM glyphs, and the views a person would open. A tool that wants ParaView images, foamcli included, calls `pgfView <case> --png <dir>`.

