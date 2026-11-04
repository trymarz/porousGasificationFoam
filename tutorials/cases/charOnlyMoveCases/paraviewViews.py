# Views for the charOnlyMoveCases fixtures (2D); each case links it as
# system/paraviewViews.py. Read by applications/utilities/paraview/pgfView.
views = [
    "meshFields",
    "solidRegime",
    "porosityThreshold",
    ("overTime", {"groups": {
        "Ts": ["regressionTsMin", "regressionTsAverage", "regressionTsCharWeighted", "regressionTsMax"],
        "porosityF": ["regressionPorosityMin", "regressionPorosityAverage"],
        "solid species": ["regressionSolidSpeciesIntegrals"],
        "inlet solid": ["regressionInletSolid"],
    }}),
]
