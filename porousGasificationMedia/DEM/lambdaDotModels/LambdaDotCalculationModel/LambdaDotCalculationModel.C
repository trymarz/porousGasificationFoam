#include "LambdaDotCalculationModel.H"
#include "constantLambdaDot.H"
#include "TsLambdaDot.H"
#include "dTsdtLambdaDot.H"

namespace Foam
{

LambdaDotCalculationModel::LambdaDotCalculationModel
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot)
{
    (void)dict;
}


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
    else if (modelName == "Ts")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new TsLambdaDot(dict, mesh, lambdaDot)
        );
    }
    else if (modelName == "dTsdt")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new dTsdtLambdaDot(dict, mesh, lambdaDot)
        );
    }

    FatalErrorInFunction
        << "Unknown lambdaMode '" << modelName << "'." << nl
        << "Valid options are: constant, Ts, dTsdt"
        << exit(FatalError);

    return autoPtr<LambdaDotCalculationModel>();
}

} // namespace Foam