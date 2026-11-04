# Views for the 3D canonical fixtures (pyrolysis, gasification, ...); each case
# links it as system/paraviewViews.py. Read by applications/utilities/paraview/pgfView.
_midZ = {"normal": [0, 0, 1]}

views = [
    ("meshFields", {"slice": _midZ}),
    ("solidRegime", {"slice": _midZ}),
    ("overTime", {"groups": {
        "Ts": ["regressionTsMin", "regressionTsAverage", "regressionTsMax"],
        "T, p average": ["regressionTAverage"],
        "porosityF": ["regressionPorosityAverage"],
        "solid mass": ["regressionWoodIntegral", "regressionCharIntegral"],
        "gas species": ["regressionO2Average", "regressionCOAverage", "regressionCO2Average"],
        "G": ["regressionGAverage"],
        "Ts, targas average": ["regressionAverage"],
    }}),
]
