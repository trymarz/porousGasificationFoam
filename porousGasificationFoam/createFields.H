#include "createRDeltaT.H"

Info<< "Reading thermophysical properties\n" << endl;
autoPtr<psiReactionThermo> pThermo(psiReactionThermo::New(mesh));
psiReactionThermo& thermo = pThermo();
thermo.validate(args.executable(), "h", "e");

basicSpecieMixture& composition = thermo.composition();
PtrList<volScalarField>& Y = composition.Y();

const word inertSpecie(thermo.lookup("inertSpecie"));
if (!composition.species().found(inertSpecie))
{
    FatalIOErrorIn(args.executable().c_str(), thermo)
        << "Inert specie " << inertSpecie << " not found in available species "
        << composition.species() << exit(FatalIOError);
}
Switch solveEnergy(thermo.lookupOrDefault("solveEnergy",true));
volScalarField rho
(
    IOobject
    (
        "rho",
        runTime.timeName(),
        mesh
    ),
    thermo.rho()
);

volScalarField& p = thermo.p();

Info<< "Reading field U\n" << endl;
volVectorField U
(
    IOobject
    (
        "U",
        runTime.timeName(),
        mesh,
        IOobject::MUST_READ,
        IOobject::AUTO_WRITE
    ),
    mesh
);

#include "compressibleCreatePhi.H"

volScalarField kappa
(
    IOobject
    (
        "kappa",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("zero", dimless, 0.0)
);

pressureControl pressureControl(p, rho, pimple.dict(), false);

mesh.setFluxRequired(p.name());

Info << "Creating turbulence model.\n" << nl;
autoPtr<compressible::turbulenceModel> turbulence
(
    compressible::turbulenceModel::New
    (
        rho,
        U,
        phi,
        thermo
    )
);

volScalarField p_thermo
(
    IOobject
    (
        "p_thermo",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("zero", p.dimensions(), 0.0)
);

dimensionedScalar timeUnit("one", dimTime, 1.0);

Info<< "Creating reaction model\n" << endl;
autoPtr<CombustionModel<psiReactionThermo>> reaction
(
    CombustionModel<psiReactionThermo>::New(thermo, turbulence())
);

Info<< "Creating field dpdt\n" << endl;
volScalarField dpdt
(
    IOobject
    (        "dpdt",
        runTime.timeName(),
        mesh
    ),
    mesh,
    dimensionedScalar(p.dimensions()/dimTime, 0)
);

Info<< "Creating field kinetic energy K\n" << endl;
volScalarField K("K", 0.5*magSqr(U));


multivariateSurfaceInterpolationScheme<scalar>::fieldTable fields;

forAll(Y, i)
{
    fields.add(Y[i]);
}
fields.add(thermo.he());

volScalarField chemistrySh
(
    IOobject
    (
        "chemistry::Sh",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::NO_WRITE
    ),
    mesh,
    dimensionedScalar("chemistrySh", dimEnergy/dimTime/dimVolume, 0.0)
);

volScalarField ddtKinEn
(
    IOobject
    (
        "ddtKinEn",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("zero", chemistrySh.dimensions(), 0.0)
);

volScalarField divPhiKinEn
(
    IOobject
    (
        "divPhiKinEn",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedScalar("zero", chemistrySh.dimensions(), 0.0)
);


Info << "Reading field Df\n" << endl;

volTensorField Df
(
        IOobject
        (
            "Df",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh
);

Info << "Reading field porosityF\n" << endl;

volScalarField porosityF
(
        IOobject
        (
            "porosityF",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh
);

volScalarField radiationF
(
        IOobject
        (
            "radiationF",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
    dimensionedScalar("zero",dimMass/dimLength/pow3(dimTime),0.0)
);

volScalarField cellVolume
(
    IOobject
    (
        "cellVolume",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::NO_WRITE
    ),
    mesh,
    dimensionedScalar("zero", dimVolume, 0.0)
);

forAll(cellVolume,cellI)
{
    cellVolume[cellI] = mesh.V()[cellI];
}

#include "readGravitationalAcceleration.H"
#include "createMRF.H"
#include "createFvOptions.H"


//------------- YADE / DEM  fields :



IOdictionary yadeCouplingProperties
{
  IOobject
  (
   "yadeProperties",
   runTime.constant(),
   mesh,
   IOobject::MUST_READ_IF_MODIFIED,
   IOobject::NO_WRITE )
};

Switch yadeActive(yadeCouplingProperties.lookupOrDefault<Switch>("active", true));


dimensionedScalar nu (
 "nu",
 dimViscosity,
 yadeCouplingProperties.lookup("nu")
);

dimensionedScalar partDensity (
    "partDensity",
     dimDensity,
     yadeCouplingProperties.lookup("partDensity")
);

dimensionedScalar fluidDensity(
    "fluidDensity",
    dimDensity,
    yadeCouplingProperties.lookup("fluidDensity")
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

// for spheres velocity sent from yade
volVectorField Us
(
    IOobject
    (
        "Us",
        runTime.timeName(),
        mesh,
        IOobject::READ_IF_PRESENT,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedVector("zero", dimVelocity, vector::zero)
);

volVectorField UsInterp
(
    IOobject
    (
        "UsInterp",
        runTime.timeName(),
        mesh,
        IOobject::READ_IF_PRESENT,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedVector("zero", dimVelocity, vector::zero)
);
