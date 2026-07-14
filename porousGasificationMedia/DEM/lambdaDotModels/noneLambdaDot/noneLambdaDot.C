#include "noneLambdaDot.H"

namespace Foam
{

noneLambdaDot::noneLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot)
{}


void noneLambdaDot::calculateTemperatureDriven()
{
    // No length-scale evolution.
    lambdaDot_ = 0.0;
}

} // namespace Foam
