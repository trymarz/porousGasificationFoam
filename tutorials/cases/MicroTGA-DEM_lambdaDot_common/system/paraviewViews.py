# Views for the MicroTGA-DEM lambdaDot fixtures; read by
# applications/utilities/paraview/pgfView (doc/paraview.md).
_centreSlice = {"normal": [0, 1, 0]}

views = [
    ("meshFields", {"slice": _centreSlice}),
    ("solidRegime", {"slice": _centreSlice}),
    ("demSpheres", {"colorBy": ["radii", "lambda", "lambdaDot"]}),
    ("demSprings", {}),
    ("overTime", {"groups": {
        "lambda": ["lambdaMin", "lambdaAverage", "lambdaMax"],
        "lambdaDot": ["lambdaDotMin", "lambdaDotAverage", "lambdaDotMax"],
    }}),
]
