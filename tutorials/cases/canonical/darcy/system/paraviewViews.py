# Views for the 1D flow fixtures darcy and forchheimer (forchheimer links this
# file). Read by applications/utilities/paraview/pgfView.
views = [
    ("meshFields", {"fields": ["p", "U", "T", "Ts", "porosityF"]}),
    ("overTime", {"groups": {
        "p": ["regressionPInlet", "regressionPOutlet"],
        "mass flow": ["regressionMassInlet", "regressionMassOutlet"],
        "U": ["regressionUAverage"],
        "T rise": ["regressionTRiseAverage", "regressionTRiseMax"],
    }}),
]
