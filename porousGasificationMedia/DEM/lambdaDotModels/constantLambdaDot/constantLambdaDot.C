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
    // Read as a bare scalar (dicts carry no dimensions) and wrapped in [m/s];
    // assigned bare it would be dimensionless and fail lambdaDot_'s check.
    lambdaValue_
    (
        dimensionedScalar
        (
            "lambdaValue",
            dimLength/dimTime,
            dict.lookupOrDefault<scalar>("lambdaValue", 0.0)
        )
    )
{}


void constantLambdaDot::calculateTemperatureDriven()
{
    // Uniform lambdaDot from constant/lambdaDict.
    lambdaDot_ = lambdaValue_;
}

} // namespace Foam
