#include "LambdaInterpolationModel.H"
#include "laplaceAnchoredLambda.H"
#include "laplaceSetValuesLambda.H"
#include "IOdictionary.H"

namespace Foam
{

LambdaInterpolationModel::LambdaInterpolationModel
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDEM,
    volScalarField& lambda,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const scalar lambdaBackgroundValue
)
:
    mesh_(mesh),
    lambdaDEM_(lambdaDEM),
    lambda_(lambda),
    nParticles_(nParticles),
    porosityF_(porosityF),
    solidPorosityCutoff_(0.0),
    lambdaBackgroundValue_(lambdaBackgroundValue)
{
    (void)dict;

    // criticalPorosity lives in pyrolysisProperties, not lambdaDict, and is
    // shared with UsInterpolationModel and lambdaDotModel so all three agree
    // on where the solid region ends.
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
            pyrolysisProperties
                .subDict("pyrolysisCoeffs")
                .lookup("criticalPorosity")
        );
}


dictionary LambdaInterpolationModel::solverControls
(
    const word& solverName
) const
{
    if (const dictionary* dictPtr = mesh_.solversDict().findDict(solverName))
    {
        return *dictPtr;
    }

    // Same controls as the "Us" entry the DEM cases carry for the equivalent
    // velocity interpolation solve.
    dictionary controls;
    controls.add("solver", word("smoothSolver"));
    controls.add("smoother", word("symGaussSeidel"));
    controls.add("tolerance", scalar(1e-6));
    controls.add("relTol", scalar(0.01));

    return controls;
}


autoPtr<LambdaInterpolationModel> LambdaInterpolationModel::New
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDEM,
    volScalarField& lambda,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const scalar lambdaBackgroundValue
)
{
    // Select the lambdaDEM-to-lambda interpolation method. Same two strategies
    // as the Us family, on their own key so lambda and Us can be smoothed
    // differently; the default matches the Us default.
    const word modelName
    (
        dict.lookupOrDefault<word>
        (
            "lambdaInterpolationMode",
            "laplaceSetValues"
        )
    );

    Info<< "Selecting lambda interpolation model: " << modelName << nl << endl;

    if (modelName == "laplaceAnchored")
    {
        return autoPtr<LambdaInterpolationModel>
        (
            new laplaceAnchoredLambda
            (
                dict,
                mesh,
                lambdaDEM,
                lambda,
                nParticles,
                porosityF,
                lambdaBackgroundValue
            )
        );
    }
    else if (modelName == "laplaceSetValues")
    {
        return autoPtr<LambdaInterpolationModel>
        (
            new laplaceSetValuesLambda
            (
                dict,
                mesh,
                lambdaDEM,
                lambda,
                nParticles,
                porosityF,
                lambdaBackgroundValue
            )
        );
    }

    FatalErrorInFunction
        << "Unknown lambdaInterpolationMode '" << modelName << "'." << nl
        << "Valid options are: laplaceAnchored, laplaceSetValues"
        << exit(FatalError);

    return autoPtr<LambdaInterpolationModel>();
}

} // namespace Foam
