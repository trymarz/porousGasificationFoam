# Views for the prescribedUsCases fixtures (Us is prescribed, so it is shown
# too); each case links it as system/paraviewViews.py. Read by pgfView.
views = [
    "meshFields",
    ("meshFields", {"fields": ["Us"]}),
    "solidRegime",
    "porosityThreshold",
    ("overTime", {"groups": {
        "porosityF": ["regressionPorosityMin", "regressionPorosityAverage", "regressionPorosityMax"],
        "Ycharm": ["regressionYcharmMin", "regressionYcharmMax"],
        "Ycharm integral": ["regressionYcharmIntegral"],
        "solid mass": ["regressionSolidMassMin", "regressionSolidMassAverage", "regressionSolidMassMax"],
        "rhos": ["regressionSolidDensityMin", "regressionSolidDensityMax"],
        "gas density": ["regressionGasDensityMin", "regressionGasDensityMax"],
        "p": ["regressionPressureMin", "regressionPressureMax"],
        "Ts": ["regressionTsCharWeighted"],
    }}),
]
