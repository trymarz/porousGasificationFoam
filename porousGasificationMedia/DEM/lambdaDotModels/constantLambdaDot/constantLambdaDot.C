#include "constantLambdaDot.H"

namespace Foam
{

constantLambdaDot::constantLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot),
    lambdaValue_(dict.lookupOrDefault<scalar>("lambdaValue", 0.0))
{}


void constantLambdaDot::calculate()
{
    // Uniform  lambdaDot from constant/lambdaDict.
    lambdaDot_ = lambdaValue_;
}

} // namespace Foam