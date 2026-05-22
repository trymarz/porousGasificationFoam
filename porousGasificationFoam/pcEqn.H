/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    pcEqn

Description
    "Consistent" PIMPLE pressure equation (SIMPLEC variant) for the
    porous-medium gas phase. Used when \c pimple.consistent() is true.
    The non-consistent variant is in pEqn.H.

    Differs from pEqn.H by splitting the diagonal of UEqn into two
    pieces: \c rAU = 1/A() and \c rAtU = 1/(1/rAU - H1()). The
    pressure solve uses \c rAtU on the Laplacian and a correction
    flux \c (rAU - rAtU) grad p is added back to HbyA. The motivation
    is to reduce under-relaxation requirements compared with the basic
    pEqn.H — at the cost of one additional Laplacian-magnitude field.

    The porosity-weighted compressibility \f$\varphi\,\psi\,\partial p/
    \partial t\f$ and the solid-to-gas source \f$\dot{m}_{s\rightarrow g}
    (1-\varphi)\f$ appear in the same form as in pEqn.H (subsonic
    branch). The transonic branch uses \c psi*p directly without the
    porosity factor on the time-derivative.

    After the solve: \c K is updated from |U|^2/2 (used by EEqn.H),
    \c pressureControl.limit(p) optionally clips p to a [pMin, pMax]
    range and refreshes rho, and \c dpdt is updated for EEqn.H.

\*---------------------------------------------------------------------------*/

rho = thermo.rho();

volScalarField rAU(1.0/UEqn.A());
volScalarField rAtU(1.0/(1.0/rAU - UEqn.H1()));
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
                fvc::interpolate(rho*rAU)*fvc::ddtCorr(rho, U, phi)
               /fvc::interpolate(rho)
            )
        )
    );

    MRF.makeRelative(fvc::interpolate(psi), phid);

    surfaceScalarField phic
    (
        "phic",
        fvc::interpolate(rho*(rAtU - rAU))*fvc::snGrad(p)*mesh.magSf()
    );

    HbyA -= (rAU - rAtU)*fvc::grad(p);

    volScalarField rhorAtU("rhorAtU", rho*rAtU);

    while (pimple.correctNonOrthogonal())
    {
        fvScalarMatrix pEqn
        (
            fvm::ddt(psi, p)
          + fvm::div(phid, p)
          + fvc::div(phic)
          - fvm::laplacian(rhorAtU, p)
         ==
            fvOptions(psi, p, rho.name())
        );

        pEqn.solve();

        if (pimple.finalNonOrthogonalIter())
        {
            phi == phic + pEqn.flux();
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
          + MRF.zeroFilter(fvc::interpolate(rho*rAU)*fvc::ddtCorr(rho, U, phi))
        )
    );

    MRF.makeRelative(fvc::interpolate(rho), phiHbyA);

    phiHbyA += fvc::interpolate(rho*(rAtU - rAU))*fvc::snGrad(p)*mesh.magSf();
    HbyA -= (rAU - rAtU)*fvc::grad(p);

    volScalarField rhorAtU("rhorAtU", rho*rAtU);

    // Update the pressure BCs to ensure flux consistency
    constrainPressure(p, rho, U, phiHbyA, rhorAtU, MRF);

    while (pimple.correctNonOrthogonal())
    {
        fvScalarMatrix pEqn
        (
            fvm::ddt(psi * porosityF, p)
          + fvc::div(phiHbyA)
          - fvm::laplacian(rhorAtU, p)
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
p.relax();

U = HbyA - rAtU*fvc::grad(p);
U.correctBoundaryConditions();
fvOptions.correct(U);
K = 0.5*magSqr(U);

if (pressureControl.limit(p))
{
    p.correctBoundaryConditions();
    rho = thermo.rho();
}

if (thermo.dpdt())
{
    dpdt = fvc::ddt(p);
}
