#include "TsLambdaDot.H"

namespace Foam
{

TsLambdaDot::TsLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot),
    lambdaFunc_(Function1<scalar>::New("lambdaDot", dict, &mesh))
{}


void TsLambdaDot::calculateTemperatureDriven()
{
    const volScalarField& Ts = mesh_.lookupObject<volScalarField>("Ts");

    // Evaluate lambdaDot = f(Ts) in each cell.
    forAll(lambdaDot_, cellI)
    {
        lambdaDot_[cellI] = lambdaFunc_->value(Ts[cellI]);
    }
}

} // namespace Foam
