#ifndef laplaceAnchored_H
#define laplaceAnchored_H

#include "UsInterpolationModel.H"

namespace Foam
{

class laplaceAnchored
:
    public UsInterpolationModel
{
private:
    scalar demVelocityAnchorCoeff_;
    scalar backgroundUsAnchorCoeff_;
    label nUsInterpolationCorrectors_;

public:
    laplaceAnchored
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volVectorField& UsDEM,
        volVectorField& Us,
        const volScalarField& nParticles,
        const volScalarField& porosityF
    );

    virtual ~laplaceAnchored() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
