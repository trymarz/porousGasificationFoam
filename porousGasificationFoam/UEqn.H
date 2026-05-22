// Solve the Momentum equation
// eqZx2uHGn043

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
