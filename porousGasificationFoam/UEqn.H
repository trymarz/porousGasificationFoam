// Solve the Momentum equation


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

pZones.addResistance(UEqn,Df);

fvOptions.constrain(UEqn);

if (pimple.momentumPredictor())
{
    solve(UEqn == -fvc::grad(p));

    fvOptions.correct(U);
}
else
{
    volScalarField rUA = 1.0 / UEqn.A();
    U = rUA * (UEqn.H() - fvc::grad(p));
    U.correctBoundaryConditions();
}

trAU = 1.0/UEqn.A();
trAU.ref().rename("rAU");
