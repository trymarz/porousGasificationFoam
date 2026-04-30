#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "fvMesh.H"
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
    volVectorField& UsDEM_;     // velocity of spheres
    volVectorField& Us_;    // interpolated sphere velocity
    volScalarField& porosityF_;   // porosity field  get from the main solver
    FoamYade& yade_;

    autoPtr<Function1<scalar>> lambdaFunc_;  // to read lambdaFunction from constant/lambdaDict


public:
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,   // velocity of spheres
        volVectorField& Us, // interpolated sphere velocity
        volScalarField& porosityF, // porosity field  get from the main solver
        FoamYade& yade
    );

    void update();
    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank

};

}

#endif // lambdaDotModel_H
