#ifndef laplaceAnchoredLambda_H
#define laplaceAnchoredLambda_H

#include "LambdaInterpolationModel.H"

namespace Foam
{

class laplaceAnchoredLambda
:
    public LambdaInterpolationModel
{
private:
    scalar demLambdaAnchorCoeff_;
    scalar backgroundLambdaAnchorCoeff_;
    label nLambdaInterpolationCorrectors_;

public:
    laplaceAnchoredLambda
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDEM,
        volScalarField& lambda,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const scalar lambdaBackgroundValue
    );

    virtual ~laplaceAnchoredLambda() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
