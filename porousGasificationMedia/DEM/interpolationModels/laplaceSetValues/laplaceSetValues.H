#ifndef laplaceSetValues_H
#define laplaceSetValues_H

#include "UsInterpolationModel.H"

namespace Foam
{

class laplaceSetValues
:
    public UsInterpolationModel
{
private:
    label nLaplaceSetValuesCorrectors_;

public:
    laplaceSetValues
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volVectorField& UsDEM,
        volVectorField& Us,
        const volScalarField& nParticles,
        const volScalarField& porosityF
    );

    virtual ~laplaceSetValues() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
