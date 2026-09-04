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
    scalar lambdaValue_;

public:
    constantLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~constantLambdaDot() = default;

    virtual void calculate() override;
};

} // namespace Foam

#endif
