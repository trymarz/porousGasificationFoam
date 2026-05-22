/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    pEqn

Description
    Standard PIMPLE pressure equation for the porous-medium gas phase.

    Used when \c pimple.consistent() is false. The consistent (SIMPLEC-
    style) variant is in pcEqn.H.

    Subsonic branch solves
    \f[
        \frac{\partial (\varphi\, \psi\, p)}{\partial t}
      + \nabla \cdot \phi_{HbyA}
      - \nabla \cdot (\rho\, rAU\, \nabla p)
      \;=\;
        S_{fvOptions}
      + \dot{m}_{s\rightarrow g}\, (1-\varphi)
    \f]

    The porosity factor \f$\varphi\f$ multiplies the compressibility-
    time-derivative term so that the storage of mass per cell is
    consistent with the porosity-weighted continuity in rhoEqn.H. The
    solid-to-gas source appears on the RHS, identical to rhoEqn.H.

    Transonic branch uses \c psi*p as the convected variable with
    \c phid and is otherwise structurally the same standard porosity-
    less formulation (no porosityF on the d/dt term in this branch).
    \c MRF terms appear unchanged from the OpenFOAM reactingFoam
    pattern.

    After the pressure solve: rhoEqn.H is re-included to re-evaluate
    the continuity residual, p and rho are explicitly relaxed (except
    on the final iteration), and U is reconstructed from HbyA - rAU
    grad p. \c dpdt is updated for the next call to EEqn.H.

\*---------------------------------------------------------------------------*/

rho = thermo.rho();

volScalarField rAU(1.0/UEqn.A());
surfaceScalarField rhorAUf("rhorAUf", fvc::interpolate(rho*rAU));
volVectorField HbyA(constrainHbyA(rAU*UEqn.H(), U, p));

if (pimple.nCorrPISO() <= 1)
{
    tUEqn.clear();
}

if (pimple.transonic())
{
    surfaceScalarField phid
    (
        "phid",
        fvc::interpolate(psi)
       *(
            fvc::flux(HbyA)
          + MRF.zeroFilter
            (
                rhorAUf*fvc::ddtCorr(rho, U, phi)/fvc::interpolate(rho)
            )
        )
    );

    MRF.makeRelative(fvc::interpolate(psi), phid);

    while (pimple.correctNonOrthogonal())
    {
        fvScalarMatrix pEqn
        (
            fvm::ddt(psi, p)
          + fvm::div(phid, p)
          - fvm::laplacian(rhorAUf, p)
         ==
            fvOptions(psi, p, rho.name())
        );

        pEqn.solve();

        if (pimple.finalNonOrthogonalIter())
        {
            phi == pEqn.flux();
        }
    }
}
else
{
    surfaceScalarField phiHbyA
    (
        "phiHbyA",
        (
            fvc::flux(rho*HbyA)
        )
    );

    MRF.makeRelative(fvc::interpolate(rho), phiHbyA);

    // Update the pressure BCs to ensure flux consistency
    constrainPressure(p, rho, U, phiHbyA, rhorAUf, MRF);

    while (pimple.correctNonOrthogonal())
    {
        fvScalarMatrix pEqn
        (
            fvm::ddt(porosityF * psi, p)
          + fvc::div(phiHbyA)
          - fvm::laplacian(rhorAUf, p)
         ==
            fvOptions(psi * porosityF, p, rho.name())
          + Srho * (1.0 - porosityF)
        );

        pEqn.solve();

        if (pimple.finalNonOrthogonalIter())
        {
            phi = phiHbyA + pEqn.flux();
        }
    }
}

#include "rhoEqn.H"
#include "compressibleContinuityErrs.H"

// Explicitly relax pressure for momentum corrector
if (!pimple.finalIter())
{
    p.relax();
    rho.relax();
}

Info<< "rho max/min : " << max(rho).value()
    << " " << min(rho).value() << endl;

U = HbyA - rAU*fvc::grad(p);
U.correctBoundaryConditions();
fvOptions.correct(U);

if (thermo.dpdt())
{
    dpdt = fvc::ddt(p);
}
