#ifndef laplaceSetValuesLambda_H
#define laplaceSetValuesLambda_H

#include "LambdaInterpolationModel.H"

namespace Foam
{

class laplaceSetValuesLambda
:
    public LambdaInterpolationModel
{
private:
    label nLaplaceSetValuesLambdaCorrectors_;

public:
    laplaceSetValuesLambda
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDEM,
        volScalarField& lambda,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const scalar lambdaBackgroundValue
    );

    virtual ~laplaceSetValuesLambda() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
