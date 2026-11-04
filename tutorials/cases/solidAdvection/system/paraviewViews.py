# Views for solidAdvection (2D, one cell thick); read by
# applications/utilities/paraview/pgfView (doc/paraview.md).
views = [
    "meshFields",
    "solidRegime",
    "porosityThreshold",
    ("overTime", {"groups": {
        "Ts": ["regressionTsMin", "regressionTsAverage", "regressionTsMax"],
    }}),
]
