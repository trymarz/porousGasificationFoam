# Views for case_line_interpolation: an L of solid in the x = 0 plane, with
# YADE spheres and springs. Read by applications/utilities/paraview/pgfView.
_midX = {"normal": [1, 0, 0]}

views = [
    ("porosityThreshold", {"ranges": {"porosityF": [0, 1]}, "camera": "+x"}),
    ("demSprings", {"camera": "+x"}),
    ("demSpheres", {"colorBy": ["radius", "velocity"], "camera": "+x"}),
    ("meshFields", {"slice": _midX}),
    ("solidRegime", {"slice": _midX}),
]
