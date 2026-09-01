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
    // lambdaValue is read as a bare scalar (the dict writes it without
    // dimensions) and wrapped in [m/s] here -- a bare scalar assigned
    // straight to lambdaDot_ would implicitly construct a dimensionless
    // dimensioned<scalar> and abort on the dimension check.
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
    // Uniform  lambdaDot from constant/lambdaDict.
    lambdaDot_ = lambdaValue_;
}

} // namespace Foam
