#include "LambdaDotCalculationModel.H"
#include "constantLambdaDot.H"
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

    if (modelName == "constant")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new constantLambdaDot(dict, mesh, lambdaDot)
        );
    }
    else if (modelName == "exactDifferential")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new exactDifferentialLambdaDot(dict, mesh, lambdaDot)
        );
    }

    // There is no separate "off" mode: exactDifferential with every
    // coefficient left at its 0.0 default gives lambdaDot = 0 exactly.
    FatalErrorInFunction
        << "Unknown lambdaMode '" << modelName << "'." << nl
        << "Valid options are: constant, exactDifferential"
        << exit(FatalError);

    return autoPtr<LambdaDotCalculationModel>();
}

} // namespace Foam
