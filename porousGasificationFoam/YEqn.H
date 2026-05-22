/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    YEqn

Description
    Transport equations for the gas-phase species mass fractions.

    For every non-inert species \f$Y_i\f$ in the gas mixture, solves
    \f[
        \frac{\partial (\varphi\, \rho\, Y_i)}{\partial t}
      + \nabla \cdot (\phi\, Y_i)
      - \nabla \cdot (\mu_{eff}\, \nabla Y_i)
      \;=\;
        R(Y_i)
      + S_{fvOptions}(\rho, Y_i)
      + S_{\rho,i}
    \f]

    where:
      - \f$\varphi\f$ is the void fraction (\c porosityF) — the gas
        species occupy only the void volume of each cell,
      - \f$\phi\f$ is the compressible mass flux (already containing
        porosity through the continuity equation),
      - \c reaction->R(Yi) is the gas-phase homogeneous reaction
        source provided by the active CombustionModel,
      - \c pyrolysisZone.Srho(i) is the heterogeneous source for
        species i, i.e. the mass of \f$Y_i\f$ produced per unit total
        cell volume per unit time by solid-to-gas reactions.

    For the inert specie the reaction source is omitted (it is the
    closure species and is determined by mass-fraction normalisation
    \f$\sum_i Y_i = 1\f$ at the end).

    Negative mass fractions arising from the linear solve are clipped
    to zero (\c Yi.max(0.0)); any value above 2 triggers a warning.
    After all species are solved, each \f$Y_i\f$ is rescaled by the
    running sum so the constraint \f$\sum_i Y_i = 1\f$ holds.

\*---------------------------------------------------------------------------*/

// Tag eqZx2uHGn041 — paper cross-reference.

tmp<fv::convectionScheme<scalar>> mvConvection
(
    fv::convectionScheme<scalar>::New
    (
        mesh,
        fields,
        phi,
        mesh.divScheme("div(phi,Yi)")
    )
);

{
    reaction->correct();
    volScalarField Yt(0.0*Y[0]);

    forAll(Y, i)
    {
        if (Y[i].name() != inertSpecie)
        {
            volScalarField& Yi = Y[i];

            // Per-species transport. The porosity factor on the
            // time-derivative term is the only place porosityF appears
            // explicitly — convection and diffusion inherit it via phi
            // and via the porosity-weighted continuity.
            fvScalarMatrix YiEqn
            (
                fvm::ddt(porosityF * rho, Yi)
              + mvConvection->fvmDiv(phi, Yi)
              - fvm::laplacian(turbulence->muEff(), Yi)
             ==
                reaction->R(Yi)
              + fvOptions(rho, Yi)
              + pyrolysisZone.Srho(i)
            );

            YiEqn.relax();

            fvOptions.constrain(YiEqn);

            YiEqn.solve("Yi");

            fvOptions.correct(Yi);

            Yi.max(0.0);
            if (gMax(Yi) > 2.0)
                Info << "warning: mass fraction exceeded 2" << endl;
            Yt += Yi;
        }
        else
        {
            // Inert specie: transported without a reaction source.
            // Heterogeneous and gas-phase reactions are not allowed to
            // produce or consume it.
            volScalarField& Yi = Y[i];

            fvScalarMatrix YiEqn
            (
                fvm::ddt(porosityF*rho, Yi)
              + mvConvection->fvmDiv(phi, Yi)
              - fvm::laplacian(turbulence->muEff(), Yi)
            );

            YiEqn.relax();

            fvOptions.constrain(YiEqn);

            YiEqn.solve("Yi");

            fvOptions.correct(Yi);

            Yi.max(0.0);
            Yt += Yi;
        }
    }

    // Renormalise so that sum_i Y_i = 1 exactly. The inert specie acts
    // as the closure and absorbs the (typically small) residual.
    forAll(Y, i)
    {
        if (Y[i].name() != inertSpecie)
        {
            Info<< " gas  " << Y[i].name() << " Srho min/max   = "
                << min(pyrolysisZone.Srho(i)()).value() << ", "
                << max(pyrolysisZone.Srho(i)()).value()
                << " min Y = " << min(Y[i]).value() <<" max Y = " << max(Y[i]).value() << endl;

            Y[i] = Y[i] / Yt;

            Info<< " min Ynorm = " << min(Y[i]).value() <<" max Ynorm = " << max(Y[i]).value() << endl;
        }
        else
        {
            Info<< " gas  " <<Y[i].name() << " is inert specie, min Y = "
                << min(Y[i]).value() <<" max Y = " << max(Y[i]).value();

            Y[i] = Y[i]/Yt;

            Info<< " min Ynorm = " << min(Y[i]).value() <<" max Ynorm = " << max(Y[i]).value() << endl;
        }
    }
}
