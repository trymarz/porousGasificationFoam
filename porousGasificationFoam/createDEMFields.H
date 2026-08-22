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


// Particle/fluid properties consumed by FoamYade::setScalarProperties() -
// drag and lift closures. The minimal pgfYade backend exchanges no forces,
// so it needs none of them.
#ifndef YADE_COUPLING_PGF
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
#endif // !YADE_COUPLING_PGF


// Cell volume fraction, filled by FoamYade's Gaussian interpolation.
#ifndef YADE_COUPLING_PGF
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
#endif // !YADE_COUPLING_PGF


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


// Fluid-side fields passed by reference into FoamYade's constructor and
// re-read (or written) by it every coupling step: the momentum-source and
// interpolation machinery of the legacy backend. The pgfYade backend applies
// no forces and needs none of them.
#ifndef YADE_COUPLING_PGF
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
#endif // !YADE_COUPLING_PGF


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


#ifndef YADE_COUPLING_PGF
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
#endif // !YADE_COUPLING_PGF
