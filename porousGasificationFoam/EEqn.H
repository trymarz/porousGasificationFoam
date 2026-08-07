// eqZx2uHGn044

if (solveEnergy)
{
    if (pyrolysisZone.equilibrium())
    {}
    else
    {
        volScalarField& he = thermo.he();
        volScalarField heatUpGas = pyrolysisZone.heatUpGas()();
        heatTransfer = pyrolysisZone.heatTransfer()();


/*
 * Diagnostic for the heat-transfer power added to the gas.
 *
 * heatTransfer is added to the gas energy equation.
 */
const dimensionedScalar gasHeatTransferPowerDiagnostic
(
    fvc::domainIntegrate(heatTransfer)
);

Info<< "INTERPHASE ENERGY DIAGNOSTIC:"
    << " gas heat-transfer source [J/s] = "
    << gasHeatTransferPowerDiagnostic.value()
    << endl;



        /*
 * ENERGY DIAGNOSTIC
 *
 * This block does not modify the energy equation.
 * It only compares:
 *
 * 1. The heatUpGas source currently returned by volPyrolysis.
 *
 * 2. The same Cp*(Ts - Tg) source reconstructed using the actual
 *    gas-mass source inserted into the continuity equation.
 *
 * 3. An approximate full sensible-enthalpy source for gas generated
 *    at the solid temperature.
 *
 * The third quantity is only diagnostic because it uses the current
 * mixture Cp rather than species-resolved pyrolysis-gas enthalpies.
 */

const dimensionedScalar pyrolysisGasMassRateDiagnostic
(
    fvc::domainIntegrate
    (
        Srho * (scalar(1.0) - porosityF)
    )
);


const dimensionedScalar storedHeatUpGasPowerDiagnostic
(
    fvc::domainIntegrate(heatUpGas)
);


/*
 * Reconstruct the present heatUpGas formula using the exact mass source
 * inserted into rhoEqn:
 *
 *     Sm * Cp * (Ts - Tg)
 */
const dimensionedScalar reconstructedHeatUpGasPowerDiagnostic
(
    fvc::domainIntegrate
    (
        Srho
      * (scalar(1.0) - porosityF)
      * thermo.Cp()
      * (pyrolysisZone.T() - thermo.T())
    )
);


/*
 * Enthalpy already associated with the newly inserted mass at the
 * current gas sensible enthalpy:
 *
 *     Sm * h(Tg)
 */
const dimensionedScalar currentGasEnthalpyPowerDiagnostic
(
    fvc::domainIntegrate
    (
        Srho
      * (scalar(1.0) - porosityF)
      * he
    )
);


/*
 * Approximate sensible enthalpy carried by gas entering at Ts:
 *
 *     Sm * [h(Tg) + Cp*(Ts - Tg)]
 *
 * This is not yet the final species-resolved correction.
 */
const dimensionedScalar approximateGeneratedGasEnthalpyPowerDiagnostic
(
    fvc::domainIntegrate
    (
        Srho
      * (scalar(1.0) - porosityF)
      *
        (
            he
          + thermo.Cp()
           *(pyrolysisZone.T() - thermo.T())
        )
    )
);

//DasteXar
// for diagnostic only, can be removed later
Info<< nl
    << "ENERGY DIAGNOSTIC:" << nl
    << "  pyrolysis gas mass rate [kg/s]             = "
    << pyrolysisGasMassRateDiagnostic.value() << nl

    << "  stored heatUpGas [J/s]                     = "
    << storedHeatUpGasPowerDiagnostic.value() << nl

    << "  reconstructed Cp*(Ts-Tg)*Sm [J/s]          = "
    << reconstructedHeatUpGasPowerDiagnostic.value() << nl

    << "  stored-reconstructed difference [J/s]      = "
    << (
        storedHeatUpGasPowerDiagnostic
      - reconstructedHeatUpGasPowerDiagnostic
    ).value() << nl

    << "  Sm*h(Tg) [J/s]                             = "
    << currentGasEnthalpyPowerDiagnostic.value() << nl

    << "  approximate Sm*h(Ts) [J/s]                 = "
    << approximateGeneratedGasEnthalpyPowerDiagnostic.value() << nl

    << "  approximate missing enthalpy [J/s]         = "
    << (
        approximateGeneratedGasEnthalpyPowerDiagnostic
      - storedHeatUpGasPowerDiagnostic
    ).value()
    << nl << endl;
// till here 



        Info<< " rho gas min/max = " << gMin(rho) << ", " << gMax(rho) << endl;
        Info<< " Sh gas min/max = " << gMin(chemistrySh) << ", " << gMax(chemistrySh) << endl;
        Info<< " porosity min/max = " << gMin(porosityF) << ", " << gMax(porosityF) << endl;
        Info<< " heatUpGas min/max = " << gMin(heatUpGas) << ", " << gMax(heatUpGas) << endl;
        Info<< " heatTransfer min/max = " << gMin(heatTransfer) << ", " << gMax(heatTransfer) << endl;

        fvScalarMatrix EEqn
        (
            fvm::ddt(porosityF * rho, he)
          + fvm::div(phi, he)
          + fvc::ddt(porosityF * rho, K)
          + fvc::div(phi, K)
          + (
                he.name() == "e"
              ? fvc::div
                (
                    fvc::absolute(phi/fvc::interpolate(porosityF * rho), U),
                    p,
                    "div(phiv,p)"
                )
              : -dpdt
            )
          - fvm::laplacian(turbulence->alphaEff(), he) 
         ==
            reaction->Qdot() * porosityF
          + fvOptions(rho, he)
          + heatTransfer
          + heatUpGas
          + radiation->Sh(thermo, he)
        );

        EEqn.relax();

        fvOptions.constrain(EEqn);

        EEqn.solve();

        fvOptions.correct(he);

        thermo.correct();

        Info<< " hs min/max = " << gMin(he) << ", " << gMax(he) << endl;
        Info<< " T gas min/max = " << gMin(T) << ", "
            << gMax(T) << endl;
    }
}
