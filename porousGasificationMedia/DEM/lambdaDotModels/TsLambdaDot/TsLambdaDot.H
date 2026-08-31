#ifndef TsLambdaDot_H
#define TsLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "Function1.H"

namespace Foam
{

class TsLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    autoPtr<Function1<scalar>> lambdaFunc_;

public:
    TsLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~TsLambdaDot() = default;

    virtual void calculateTemperatureDriven() override;
};

} // namespace Foam

#endif
