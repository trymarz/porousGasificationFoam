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

Info << "DEM coupling: "
     << (DEM ? "active" : "inactive (yadeProperties absent or active=false)")
     << nl;

if (yadeCouplingProperties.size() > 0 && !DEM)
    WarningIn("createDEMFields.H")
        << "yadeProperties found but DEM coupling is disabled (active != true)" << nl;


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


// Number of DEM particle centres in each cell; 0 marks an empty cell.
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

// lambdaDot [m/s]: rate of change of the particle length scale. Assembled by
// volPyrolysis::solveSpeciesMass(); lambdaDotModel gates it outside the solid
// region and pushes it to the YADE particles.
volScalarField lambdaDot
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
    dimensionedScalar("0", dimLength/dimTime, 0)
);

// lambda [m]: particle length scale, interpolated from lambdaDEM. Derived
// diagnostic -- YADE owns the integration (State::lambda_) and no PGF
// equation consumes it. Default 1.0 matches YADE's State::lambda_; a 0/lambda
// file must carry dimensions [0 1 0 0 0 0 0]. zeroGradient rather than
// calculated, because the interpolation models solve an implicit Laplacian on
// it and calculatedFvPatchField has no gradientInternalCoeffs.
volScalarField lambda
(
    IOobject
    (
        "lambda",
        runTime.timeName(),
        mesh,
        IOobject::READ_IF_PRESENT,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("one", dimLength, 1.0),
    "zeroGradient"
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
    IOobject::AUTO_WRITE),
    mesh,
    dimensionedVector("UsDEM", dimensionSet(0,1,-1,0,0,0,0), vector(0,0,0) )
);


// lambdaDEM [m]: raw per-cell average of particle lambda, UsDEM's scalar
// counterpart. Rebuilt every exchange (NO_READ); empty cells stay zero, and
// lambda rather than this field carries a defined value everywhere.
volScalarField lambdaDEM(
    IOobject(
    "lambdaDEM",
    runTime.timeName(),
    mesh,
    IOobject::NO_READ,
    IOobject::AUTO_WRITE),
    mesh,
    dimensionedScalar("lambdaDEM", dimLength, 0.0 )
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


