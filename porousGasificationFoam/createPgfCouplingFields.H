// Minimal field creation for the PGF-native YADE coupling.
//
// NOTE: this fragment is NOT currently #included by the solver. createDEMFields.H
// declares nParticles, lambdaDot and UsDEM at function scope for *both* backends
// (they must stay in scope for the coupling-glue includes that follow). It is kept
// here as the Phase-3 artifact: once the legacy FoamYade fields are deleted from
// createDEMFields.H, that file collapses to exactly this minimal set.
//
// No drag/lift/torque fields — the PGF coupling only needs particle counts,
// lambdaDot (shrink signal) and UsDEM (solid velocity from spheres). Us itself
// is created in createFields.H and shared with the rest of the solver.

volScalarField nParticles
(
    IOobject("nParticles", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    mesh,
    dimensionedScalar("zero", dimless, 0.0)
);

volScalarField lambdaDot
(
    IOobject("lambdaDot", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    mesh,
    dimensionedScalar("0", dimless, 0)
);

volVectorField UsDEM
(
    IOobject("UsDEM", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    mesh,
    dimensionedVector("UsDEM", dimVelocity, vector::zero)
);
