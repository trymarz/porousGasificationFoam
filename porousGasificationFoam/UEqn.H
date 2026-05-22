/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    UEqn

Description
    Momentum predictor for the gas phase inside the PIMPLE loop.

    Solves the compressible momentum balance
    \f[
        \frac{\partial (\rho U)}{\partial t}
      + \nabla \cdot (\phi\, U)
      + (\mathrm{MRF\ frame\ term})
      + \nabla \cdot \tau_{eff}(U)
      \;=\;
        \rho g
      + S_{fvOptions}
      - \nabla p
      - R_{porous}(U)
    \f]

    where:
      - \c phi is the compressible mass flux on faces,
      - \c MRF.DDt is the multiple-reference-frame frame-rotation term
        (zero in non-rotating cases),
      - \c turbulence->divDevRhoReff is the effective (laminar +
        turbulent) deviatoric stress divergence,
      - \c R_{porous} is the porous resistance contributed by
        \c pZones.addResistance(UEqn,Df), which is the linear
        Darcy term \f$\mu_{eff} D U\f$ plus, when enabled, the
        quadratic Forchheimer term (see \c fieldPorosityModel).

    The phase indicator \c porosityF does NOT appear explicitly in this
    equation: the porosity influence on the gas momentum is carried by
    the resistance tensor \c Df (large values where the gas is excluded
    by the solid, small values inside the porous region) and by the
    porosity-weighted continuity used elsewhere.

    \c pimple.momentumPredictor() controls whether the system is
    solved here (full momentum predictor) or only the matrix is
    assembled and U is reconstructed explicitly from H/A.

\*---------------------------------------------------------------------------*/

// Tag eqZx2uHGn043 — paper cross-reference, do not remove without checking
// the documentation index it ties into.

MRF.correctBoundaryVelocity(U);

tmp<fvVectorMatrix> tUEqn
(
    fvm::ddt(rho, U) + fvm::div(phi, U)
  + MRF.DDt(rho, U)
  + turbulence->divDevRhoReff(U)
 ==
    rho*g
  + fvOptions(rho, U)
);
fvVectorMatrix& UEqn = tUEqn.ref();

UEqn.relax();

tmp<volScalarField> trAU;
tmp<volTensorField> trTU;

// Add the porous resistance term R_porous(U) to UEqn.
// Df is the (anisotropic) Darcy resistance tensor read from constant/Df;
// the Forchheimer scalar (if any) is read from porosityProperties by
// fieldPorosityModel and added on top. Tag eqZx2uHGn015 references the
// porous-resistance equation in the paper.
pZones.addResistance(UEqn,Df);

fvOptions.constrain(UEqn);

if (pimple.momentumPredictor())
{
    // Full predictor: solve U from UEqn with -grad(p) on the RHS.
    solve(UEqn == -fvc::grad(p));

    fvOptions.correct(U);
}
else
{
    // No predictor: reconstruct U from the diagonal A() and off-diagonal H()
    // operators of UEqn. Used when momentumPredictor is disabled in
    // system/fvSolution.
    volScalarField rUA = 1.0 / UEqn.A();
    U = rUA * (UEqn.H() - fvc::grad(p));
    U.correctBoundaryConditions();
}

trAU = 1.0/UEqn.A();
trAU.ref().rename("rAU");
