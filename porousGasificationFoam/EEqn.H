/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    EEqn

Description
    Gas-phase energy equation (sensible enthalpy or internal energy).

    Solves
    \f[
        \frac{\partial (\varphi\, \rho\, h_e)}{\partial t}
      + \nabla \cdot (\phi\, h_e)
      + \frac{\partial (\varphi\, \rho\, K)}{\partial t}
      + \nabla \cdot (\phi\, K)
      + \Pi(p)
      - \nabla \cdot (\alpha_{eff}\, \nabla h_e)
      \;=\;
        \dot{Q}_{chem}\, \varphi
      + S_{fvOptions}
      + Q_{HT}
      + \dot{m}_{pyr}\, h_{up}
      + S_{rad}
    \f]

    where:
      - \f$h_e\f$ is either sensible enthalpy \c hs or internal
        energy \c e, selected by the thermo type;
      - \f$K = \tfrac{1}{2}|U|^2\f$ is the kinetic energy field;
      - \f$\Pi(p)\f$ is the pressure-work term:
        \c +div(phiv,p) when solving for internal energy and
        \c -dpdt when solving for enthalpy;
      - \f$\varphi\f$ is the porosity (\c porosityF) so the equation
        is per unit total cell volume, not per unit void volume;
      - \f$\dot{Q}_{chem}\f$ is the homogeneous heat release rate
        from \c reaction->Qdot(), scaled by \f$\varphi\f$ because
        gas-phase reactions only occur in the void volume;
      - \f$Q_{HT}\f$ is the interphase convective heat transfer
        \c pyrolysisZone.heatTransfer() = h SAV (T_s - T) [W/m^3];
      - \f$\dot{m}_{pyr}\,h_{up}\f$ is the enthalpy needed to bring
        the gas freshly released by pyrolysis from \f$T_s\f$ to the
        local gas state \c pyrolysisZone.heatUpGas();
      - \f$S_{rad}\f$ is the radiation contribution
        \c radiation->Sh(thermo, he).

    If \c solveEnergy is false (set in thermophysicalProperties) the
    equation is skipped entirely. The \c equilibrium() branch of the
    pyrolysis model also short-circuits the gas energy update because
    in that mode gas and solid temperatures are imposed equal.

\*---------------------------------------------------------------------------*/

// Tag eqZx2uHGn044 — paper cross-reference.

if (solveEnergy)
{
    if (pyrolysisZone.equilibrium())
    {
        // Equilibrium (Ts == T) — gas energy is not advanced
        // independently. The solid energy equation in volPyrolysis
        // owns the temperature update in this regime.
    }
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
