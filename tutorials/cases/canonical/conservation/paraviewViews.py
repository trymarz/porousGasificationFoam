# Views for the conservation fixtures (one or a few cells, so charts only);
# each case links it as system/paraviewViews.py. Read by pgfView.
views = [
    ("overTime", {"groups": {
        "residuals": ["regressionHeatTransferResidual", "regressionGasEnergyResidual",
                      "regressionRadiationBookResidual"],
        "heat transfer": ["regressionHeatTransferExchanged", "regressionHeatTransfer",
                          "regressionGasProductEnthalpyAtTs",
                          "regressionGasReactantEnthalpyAtTg",
                          "regressionGasInterfaceEnthalpy",
                          "regressionHeatUpGasReactantsToTs"],
        "chemistry": ["regressionChemistryHeat", "regressionChemistryHeatOutsideGate",
                      "regressionGasProductEnthalpyAtTsOutsideGate",
                      "regressionGasReactantEnthalpyAtTgOutsideGate",
                      "regressionHeatUpGasReactantsToTsOutsideGate",
                      "regressionChemistrySh"],
        "gas sources": ["regressionGasSourceOutsideGate", "regressionSrho"],
        "radiation": ["regressionRadiationBookedMin", "regressionRadiationBookedMax",
                      "regressionRadiationWallFlux", "regressionRadiationSh"],
        "G": ["regressionGAverage"],
        "temperature": ["regressionTsAverage", "regressionTsMax", "regressionTAverage",
                        "regressionGasTempMax"],
        "solid": ["regressionSolidMass", "regressionSolidSensible"],
        "porosityF": ["regressionPorosityMax"],
        "p": ["regressionPAverage"],
    }}),
]
