#ifndef UsInterpolationModel_H
#define UsInterpolationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

class UsInterpolationModel
{
protected:
    const fvMesh& mesh_;
    volVectorField& UsDEM_;
    volVectorField& Us_;
    const volScalarField& nParticles_;
    const volScalarField& porosityF_;

    scalar solidPorosityCutoff_;

public:
    UsInterpolationModel
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volVectorField& UsDEM,
        volVectorField& Us,
        const volScalarField& nParticles,
        const volScalarField& porosityF
    );

    virtual ~UsInterpolationModel() = default;

    static autoPtr<UsInterpolationModel> New
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volVectorField& UsDEM,
        volVectorField& Us,
        const volScalarField& nParticles,
        const volScalarField& porosityF
    );

    virtual void interpolate() = 0;
};

} // namespace Foam

#endif
