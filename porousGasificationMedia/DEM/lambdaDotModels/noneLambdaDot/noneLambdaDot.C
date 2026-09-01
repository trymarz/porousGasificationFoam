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
    // No length-scale evolution. A bare 0.0 implicitly constructs a
    // dimensionless dimensioned<scalar>, which aborts on assignment to
    // lambdaDot_ ([m/s]) -- the zero must carry the same dimensions.
    lambdaDot_ = dimensionedScalar("0", dimLength/dimTime, 0.0);
}

} // namespace Foam
