/*---------------------------------------------------------------------------*\
Global
    createDEMFields

Description
    Construct fields and constants used by the YADE/DEM coupling. Only
    included when the solver is compiled with \c WITH_YADE.

    Reads \c constant/yadeProperties. Behaviour is gated by the \c DEM
    switch (key \c active in that dictionary): when false, the fields
    are still constructed (because they are referenced unconditionally
    by the lambdaDotUpdater object), but the DEM block in the main
    loop is skipped.

    Field families:

      - Particle scalars per cell: \c alphac (gas volume fraction
        carried by YADE), \c nParticles (number of spheres in cell),
        \c lambdaDot (rate of per-particle lambda change — drives
        porosity evolution from the DEM side).
      - Gas-side gradients used by the drag closure: \c gradP,
        \c vGrad, \c divT, \c ddtU_f.
      - DEM->fluid coupling sources: \c uSource, \c uSourceDrag,
        \c uCoeff (consumed by the YADE coupling to add momentum
        sources to UEqn — TODO: confirm exactly which path during
        the walkthrough; current solver loop does not appear to inject
        these into UEqn.H directly).
      - Velocities: \c uParticle (per-particle aggregate),
        \c UsDEM (per-cell average of particle velocity — the raw,
        unsmoothed DEM solid velocity field), \c uInterp (gas
        velocity at particle locations, for the YADE-side drag).
        The Laplace-smoothed solid velocity Us was created in
        createFields.H; lambdaDotModel::update() in the main loop
        writes Us from UsDEM by neighbour-averaging into adjacent
        solid cells.

\*---------------------------------------------------------------------------*/

IOdictionary yadeCouplingProperties
{
  IOobject
  (
   "yadeProperties",
   runTime.constant(),
   mesh,
   IOobject::READ_IF_PRESENT,
   IOobject::NO_WRITE )
};

Switch DEM(yadeCouplingProperties.lookupOrDefault<Switch>("active", false));


dimensionedScalar nu (
    "nu",
    dimViscosity,
    yadeCouplingProperties.lookupOrDefault<scalar>("nu", 0.0)
);

dimensionedScalar partDensity (
    "partDensity",
    dimDensity,
    yadeCouplingProperties.lookupOrDefault<scalar>("partDensity", 0.0)
);

dimensionedScalar fluidDensity(
    "fluidDensity",
    dimDensity,
    yadeCouplingProperties.lookupOrDefault<scalar>("fluidDensity", 0.0)
);


volScalarField alphac
(
    IOobject
    (
    "alpha",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("0", dimless, 0)
);


// DasteXar this field determines if there is any sphere in the cell or no
volScalarField nParticles
(
    IOobject
    (
        "nParticles",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("zero", dimless, 0.0)
);

//DasteXar  this field (lambdaDot) represents the rate of Lambda change  in each cell
volScalarField lambdaDot // lambdaDot: rate of change of per-particle lambda (computed by lambdaDotModel)
(
    IOobject
    (
        "lambdaDot",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("0", dimless, 0)
);


volVectorField divT(
    IOobject(
    "divT",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    fvc::laplacian(U)
);

volTensorField vGrad(
    IOobject(
    "vGrad",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    fvc::grad(U)
    );


volVectorField gradP(
    IOobject(
    "gradP",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::AUTO_WRITE),
    fvc::grad(p)/rho
);



volScalarField uSourceDrag(
    IOobject(
    "uSourceDrag",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    mesh,
    dimensionedScalar("dragCoeff", dimensionSet(0,0,-1,0,0,0,0), 0 )
);


volVectorField uSource(
    IOobject(
    "uSource",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    mesh,
    dimensionedVector("volSource", dimensionSet(0,1,-2,0,0,0,0), vector(0,0,0) )
);

volScalarField uCoeff(
    IOobject(
    "uCoeffDrag",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    mesh,
    dimensionedScalar("dragCoeffFluid", dimensionSet(0,0,-1,0,0,0,0), 0 )
);

volVectorField uInterp(
    IOobject(
    "uInterpField",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    mesh,
    dimensionedVector("interpVel", dimensionSet(0,1,-1,0,0,0,0), vector(0,0,0) )
);


volVectorField uParticle(
    IOobject(
    "uParticle",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    mesh,
    dimensionedVector("uParticle", dimensionSet(0,1,-1,0,0,0,0), vector(0,0,0) )
);


volVectorField UsDEM(
    IOobject(
    "UsDEM",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    mesh,
    dimensionedVector("UsDEM", dimensionSet(0,1,-1,0,0,0,0), vector(0,0,0) )
);


volVectorField ddtU_f (
    IOobject (
    "ddtU_f",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::NO_WRITE),
    //fvc::ddt(U)+fvc::div(phi, U)
    ( fvc::ddt(rho, U) + fvc::div(phi, U) ) / rho
);


