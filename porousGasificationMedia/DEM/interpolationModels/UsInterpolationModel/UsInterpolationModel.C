#include "UsInterpolationModel.H"
#include "laplaceAnchored.H"
#include "laplaceSetValues.H"
#include "IOdictionary.H"

namespace Foam
{

UsInterpolationModel::UsInterpolationModel
(
    const dictionary& dict,
    const fvMesh& mesh,
    volVectorField& UsDEM,
    volVectorField& Us,
    const volScalarField& nParticles,
    const volScalarField& porosityF
)
:
    mesh_(mesh),
    UsDEM_(UsDEM),
    Us_(Us),
    nParticles_(nParticles),
    porosityF_(porosityF),
    solidPorosityCutoff_(0.0)
{
    (void)dict;
    IOdictionary pyrolysisProperties
    (
        IOobject
        (
            "pyrolysisProperties",
            mesh.time().constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    solidPorosityCutoff_ =
    readScalar
    (
        pyrolysisProperties.subDict("pyrolysisCoeffs").lookup("criticalPorosity")
    );

    Info<< "criticalPorosity = "
        << solidPorosityCutoff_ << nl << endl;
}


//DasteXar interpolation

autoPtr<UsInterpolationModel> UsInterpolationModel::New
(
    const dictionary& dict,
    const fvMesh& mesh,
    volVectorField& UsDEM,
    volVectorField& Us,
    const volScalarField& nParticles,
    const volScalarField& porosityF
)
{
    // Select the UsDEM-to-Us interpolation method:
    // laplaceAnchored keeps the current soft anchored diffusion solve,
    // laplaceSetValues uses the pgfVeloInt hard setValues Laplace solve.
    const word modelName
    (
        dict.lookupOrDefault<word>
        (
            "interpolationMode",
            "laplaceSetValues" // primary logic
        )
    );

    Info<< "Selecting Us interpolation model: " << modelName << nl << endl;

    if (modelName == "laplaceAnchored")
    {
        return autoPtr<UsInterpolationModel>
        (
            new laplaceAnchored
            (
                dict,
                mesh,
                UsDEM,
                Us,
                nParticles,
                porosityF
            )
        );
    }
    else if (modelName == "laplaceSetValues")
    {
        return autoPtr<UsInterpolationModel>
        (
            new laplaceSetValues
            (
                dict,
                mesh,
                UsDEM,
                Us,
                nParticles,
                porosityF
            )
        );
    }

    FatalErrorInFunction
        << "Unknown interpolationMode '" << modelName << "'." << nl
        << "Valid options are: laplaceAnchored, laplaceSetValues"
        << exit(FatalError);

    return autoPtr<UsInterpolationModel>();
}

} // namespace Foam
