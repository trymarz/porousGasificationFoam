/*---------------------------------------------------------------------------*\
Description
    Gas-phase sensible-enthalpy transport in the porous bed:

        d(porosityF*rho*he)/dt + div(phi,he) + d(porosityF*rho*K)/dt
      + div(phi,K) - porosityF*dp/dt - laplacian(alphaEff,he)
      = Qdot*porosityF + heatTransfer
      + (gasProductEnthalpyAtTs + gasReactantEnthalpyAtTg) + radiation->Sh

    Like the storage term, the pressure work is per unit total volume, so
    dp/dt carries the gas fraction porosityF. The he = e form follows from
    h = e + p/rho; with phi the superficial mass flux (rhoEqn.H), its
    pressure work is div(phi/rho, p) + p*d(porosityF)/dt.

    porosityF is itself time-varying (the solid converts to gas), so the
    accumulation term's product rule has a he*d(porosityF*rho)/dt piece
    riding along with porosityF*rho*d(he)/dt (likewise for K).
    fvm::ddt(weight, he) and fvc::ddt(weight, K) only assemble that
    correctly if "weight" is a real field whose oldTime()
    holds the true previous-timestep porosityF*rho, not a copy of itself:
    porosityF*rho built inline is a fresh temporary every call, so its
    ddt silently drops (rho-rho_old)*porosityF_old + rho_old*(porosityF-
    porosityF_old) each step. porosityRho (createFields.H) is the
    registered, stored-old-time stand-in that avoids this; YEqn.H shares
    the same weight and must stay consistent with it.

    gasProductEnthalpyAtTs and gasReactantEnthalpyAtTg are the sensible
    enthalpy h of the mass Srho_i brings across the interface (products at
    Ts, gaseous reactants at Tg; GasSolidHeatExchange), h and not e even in
    e form: entering mass brings its flow work. A continuity-error term
    such as fvm::Sp(contErr, he) must exclude sum_i Srho_i, or gas
    products arrive at Tg again (#213).

    With frozenTemperature no energy equation is solved: gas and solid keep
    their own, separate temperatures.
\*---------------------------------------------------------------------------*/

if (solveEnergy)
{
    if (pyrolysisZone.frozenTemperature())
    {}
    else
    {
        volScalarField& he = thermo.he();
        volScalarField gasProductEnthalpyAtTs =
            pyrolysisZone.gasProductEnthalpyAtTs()();
        volScalarField gasReactantEnthalpyAtTg =
            pyrolysisZone.gasReactantEnthalpyAtTg()();
        heatTransfer = pyrolysisZone.heatTransfer()();

        Info<< " rho gas min/max = " << gMin(rho) << ", " << gMax(rho) << endl;
        Info<< " Sh gas min/max = " << gMin(chemistrySh) << ", " << gMax(chemistrySh) << endl;
        Info<< " porosity min/max = " << gMin(porosityF) << ", " << gMax(porosityF) << endl;
        Info<< " gasProductEnthalpyAtTs min/max = " << gMin(gasProductEnthalpyAtTs) << ", " << gMax(gasProductEnthalpyAtTs) << endl;
        Info<< " gasReactantEnthalpyAtTg min/max = " << gMin(gasReactantEnthalpyAtTg) << ", " << gMax(gasReactantEnthalpyAtTg) << endl;
        Info<< " heatTransfer min/max = " << gMin(heatTransfer) << ", " << gMax(heatTransfer) << endl;

        fvScalarMatrix EEqn
        (
            fvm::ddt(porosityRho, he)
          + fvm::div(phi, he)
          + fvc::ddt(porosityRho, K)
          + fvc::div(phi, K)
          + (
                he.name() == "e"
              ? fvc::div
                (
                    fvc::absolute(phi/fvc::interpolate(rho), U),
                    p,
                    "div(phiv,p)"
                )
              + p*fvc::ddt(porosityF)
              : -porosityF*dpdt
            )
          - fvm::laplacian(turbulence->alphaEff(), he) 
         ==
            reaction->Qdot() * porosityF
          + fvOptions(rho, he)
          + heatTransfer
            // Summed first, as the single field they split, so round-off
            // matches the one-source form.
          + (gasProductEnthalpyAtTs + gasReactantEnthalpyAtTg)
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
