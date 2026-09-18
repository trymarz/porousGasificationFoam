/*---------------------------------------------------------------------------*\
Description
    Gas-phase sensible-enthalpy transport in the porous bed:

        d(porosityF*rho*he)/dt + div(phi,he) + d(porosityF*rho*K)/dt
      + div(phi,K) + div(phiv,p) - laplacian(alphaEff,he)
      = Qdot*porosityF + heatTransfer + heatUpGas + radiation->Sh

    porosityF is itself time-varying (the solid converts to gas), so the
    accumulation term's product rule has a he*d(porosityF*rho)/dt piece
    riding along with porosityF*rho*d(he)/dt. fvm::ddt(weight, he) only
    assembles that correctly if "weight" is a real field whose oldTime()
    holds the true previous-timestep porosityF*rho, not a copy of itself:
    porosityF*rho built inline is a fresh temporary every call, so its
    ddt silently drops (rho-rho_old)*porosityF_old + rho_old*(porosityF-
    porosityF_old) each step. porosityRho (createFields.H) is the
    registered, stored-old-time stand-in that avoids this; YEqn.H shares
    the same weight and must stay consistent with it.
\*---------------------------------------------------------------------------*/
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

        Info<< " rho gas min/max = " << gMin(rho) << ", " << gMax(rho) << endl;
        Info<< " Sh gas min/max = " << gMin(chemistrySh) << ", " << gMax(chemistrySh) << endl;
        Info<< " porosity min/max = " << gMin(porosityF) << ", " << gMax(porosityF) << endl;
        Info<< " heatUpGas min/max = " << gMin(heatUpGas) << ", " << gMax(heatUpGas) << endl;
        Info<< " heatTransfer min/max = " << gMin(heatTransfer) << ", " << gMax(heatTransfer) << endl;

        fvScalarMatrix EEqn
        (
            fvm::ddt(porosityRho, he)
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
