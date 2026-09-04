#ifndef constantLambdaDot_H
#define constantLambdaDot_H

#include "LambdaDotCalculationModel.H"

namespace Foam
{

class constantLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    dimensionedScalar lambdaValue_;

public:
    constantLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~constantLambdaDot() = default;

    virtual void calculateTemperatureDriven() override;
};

} // namespace Foam

#endif
