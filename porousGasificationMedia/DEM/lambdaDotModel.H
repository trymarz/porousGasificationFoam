#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "FoamYade.H"
#include "Function1.H"


namespace Foam
{

class lambdaDotModel
{
private:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;
    volScalarField& nParticles_;  // number of particles
    volVectorField& Us_;     // velocity of spheres
    FoamYade& yade_;

    autoPtr<Function1<scalar>> lambdaFunc_;  // to read lambdaFunction from constant/lambdaDict


public:
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& Us,   // velocity of spheres
        FoamYade& yade
    );

    void update();
    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank

};

}


#endif


