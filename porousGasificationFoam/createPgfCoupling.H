// PGF-native YADE coupling glue (parallel to the legacy createYadeCoupling.H).
//
// Instantiated only when yadeProperties sets couplingBackend = pgf. Builds the
// stripped PgfYadeCoupling (MPI init + particle receive + lambdaDot send) and
// wires it into lambdaDotModel via the PgfYadeCoupling constructor overload.
// No fluid-field references are passed — the fluid does not push back on
// particles, so there are no drag/torque/source fields here.

#include "PgfYadeCoupling.H"

autoPtr<PgfYadeCoupling> pgfCoupling(nullptr);
autoPtr<lambdaDotModel>  pgfLambdaDotUpdater(nullptr);

if (DEM && couplingBackend == "pgf")
{
    pgfCoupling.reset(new PgfYadeCoupling(mesh, lambdaDot));

    // Ts is kept for interface compatibility (it no longer drives shrinkage);
    // Ychar is the solid char mass fraction that drives the per-cell shrink
    // factor inside lambdaDotModel::update(). Both are guaranteed on the mesh
    // by the time this file runs (after createPyrolysisModel.H).
    const volScalarField& Ts    = mesh.lookupObject<volScalarField>("Ts");
    const volScalarField& Ychar = mesh.lookupObject<volScalarField>("Ychar");

    pgfLambdaDotUpdater.reset(new lambdaDotModel(
        mesh, lambdaDot, nParticles, UsDEM, Us, porosityF,
        Ts, Ychar, pgfCoupling()));

    // No locateAllParticles() at startup — particles have not arrived yet.
    // The first setParticleAction() call handles the initial location.
}
