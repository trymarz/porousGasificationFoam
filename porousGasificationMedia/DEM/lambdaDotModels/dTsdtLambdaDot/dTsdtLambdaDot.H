#ifndef dTsdtLambdaDot_H
#define dTsdtLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "Function1.H"
#include "scalarField.H"

namespace Foam
{

class dTsdtLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    autoPtr<Function1<scalar>> lambdaFunc_;

    // Stored Ts snapshot from the previous completed thermal step.
    scalarField TsForLambdaOld_;

    // deltaT used by the last completed thermal step.
    scalar lambdaDeltaT_;

    // First call has no completed Ts increment yet.
    bool haveTsForLambdaOld_;

public:
    dTsdtLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~dTsdtLambdaDot() = default;

    virtual void calculate() override;
};

} // namespace Foam

#endif