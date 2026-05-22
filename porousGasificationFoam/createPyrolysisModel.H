/*---------------------------------------------------------------------------*\
Global
    createPyrolysisModel

Description
    Construct the solid thermophysical model and the heterogeneous
    pyrolysis model, plus the two coupling source fields they expose.

    Two object hierarchies are built:

      1. HGSSolidThermo: solid-phase thermophysics (intrinsic density,
         specific heat, conductivity, radiation properties of each
         component, and the mixture rules). Reads
         \c constant/solidThermophysicalProperties. The gas-side Y list
         is passed in so the mixture can identify which solid
         components map to which gas species during reactions.

      2. heterogeneousPyrolysisModel (typically \c volPyrolysis):
         orchestrates solid species mass conservation, the per-cell
         chemistry ODE (via porousThermoSolidChemistryModel), the
         solid energy equation, optional bed-collapse motion, and
         optional replenishment. Reads \c constant/pyrolysisProperties
         and \c constant/chemistryProperties.

    Two derived fields are published into the solver scope so they can
    be re-used downstream (pEqn.H / rhoEqn.H use Srho; EEqn.H uses
    heatTransfer indirectly via pyrolysisZone.heatTransfer()):

      - Srho:         solid-to-gas mass production per unit solid volume
                      [kg/m^3/s] (see rhoEqn.H for the (1-porosityF)
                      scaling on use).
      - heatTransfer: volumetric interphase convective heat transfer
                      [W/m^3] from pyrolysisZone.

\*---------------------------------------------------------------------------*/

Info<< "Reading solid termophysical properties" << endl;

IOdictionary chemProperties
(
    IOobject
    (
        "chemistryProperties",
        mesh.time().constant(),
        mesh,
        IOobject::MUST_READ_IF_MODIFIED,
        IOobject::NO_WRITE
    )
);

autoPtr<HGSSolidThermo> sThermo
(
    HGSSolidThermo::New
    (
         mesh,
         IOdictionary
             (
                 IOobject
                 (
                     "solidThermophysicalProperties",
                     mesh.time().constant(),
                     mesh,
                     IOobject::MUST_READ,
                     IOobject::NO_WRITE,
                     false
                 )
             ),
        Y
    )
);

HGSSolidThermo& solidThermo = sThermo();


Info<< "\nCreating pyrolysis model" << endl;

typedef typename heterogeneousPyrolysisModels::heterogeneousPyrolysisModel pyrolysisModelType;

autoPtr<pyrolysisModelType> pyrolysis
(
    pyrolysisModelType::New(mesh, solidThermo, thermo, porosityF, radiationF)
);

pyrolysisModelType& pyrolysisZone = pyrolysis();

volScalarField Srho
(
    IOobject
    (
        "Srho",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    pyrolysisZone.Srho()
);

volScalarField heatTransfer
(
    IOobject
    (
        "heatTransfer",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    pyrolysisZone.heatTransfer()
);
