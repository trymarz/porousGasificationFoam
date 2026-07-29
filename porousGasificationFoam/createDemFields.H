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

Switch demActive(yadeCouplingProperties.lookupOrDefault<Switch>("active", false));

Info << "DEM coupling: "
     << (demActive ? "active" : "inactive (yadeProperties absent or active=false)")
     << nl;

if (yadeCouplingProperties.size() > 0 && !demActive)
    WarningIn("createDemFields.H")
        << "yadeProperties found but DEM coupling is disabled (active != true)" << nl;


// number of DEM spheres located in each cell
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

// lambdaDot: rate of change of the per-particle characteristic length scale
// lambda; computed on the FVM side and sent to the DEM spheres
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
    dimensionedScalar("0", dimless, 0)
);

// lambda: integrated DEM particle characteristic length scale, advanced by
// volPyrolysis::updateCurrentLambda() as lambda += lambdaDot*dt. Unlike the
// rate lambdaDot (recomputed every step), this is genuine integrated state,
// so it is read back on restart. The default 1.0 matches YADE's convention
// for a sphere whose radius has not changed.
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
    dimensionedScalar("1.0", dimless, 1.0)
);

// cell-averaged velocity of the DEM spheres (raw, before interpolation to Us)
volVectorField UsDEM
(
    IOobject
    (
        "UsDEM",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedVector("UsDEM", dimensionSet(0,1,-1,0,0,0,0), vector(0,0,0))
);

volTensorField vGrad
(
    IOobject
    (
        "vGrad",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::NO_WRITE
    ),
    fvc::grad(U)
);
