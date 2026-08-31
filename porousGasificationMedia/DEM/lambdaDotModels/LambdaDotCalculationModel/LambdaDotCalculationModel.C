#include "LambdaDotCalculationModel.H"
#include "noneLambdaDot.H"
#include "constantLambdaDot.H"
#include "TsLambdaDot.H"
#include "exactDifferentialLambdaDot.H"

namespace Foam
{

LambdaDotCalculationModel::LambdaDotCalculationModel
(
    const dictionary&,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot)
{}


autoPtr<LambdaDotCalculationModel> LambdaDotCalculationModel::New
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
{
    const word modelName
    (
        dict.lookupOrDefault<word>("lambdaMode", "constant")
    );

    Info<< "Selecting lambdaDot calculation model: "
        << modelName << nl << endl;

    if (modelName == "none")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new noneLambdaDot(dict, mesh, lambdaDot)
        );
    }
    else if (modelName == "constant")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new constantLambdaDot(dict, mesh, lambdaDot)
        );
    }
    else if (modelName == "Ts")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new TsLambdaDot(dict, mesh, lambdaDot)
        );
    }
    else if (modelName == "exactDifferential")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new exactDifferentialLambdaDot(dict, mesh, lambdaDot)
        );
    }

    FatalErrorInFunction
        << "Unknown lambdaMode '" << modelName << "'." << nl
        << "Valid options are: none, constant, Ts, exactDifferential"
        << exit(FatalError);

    return autoPtr<LambdaDotCalculationModel>();
}

} // namespace Foam
