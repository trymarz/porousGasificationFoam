#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "fvMesh.H"
#include "FoamYade.H"          // legacy coupling backend
#include "PgfYadeCoupling.H"   // PGF-native coupling backend
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
    const volScalarField& Ts_;    // solid temperature (kept for interface compatibility; no longer drives shrinkage)
    const volScalarField& Ychar_; // solid char mass fraction (drives shrinkage)

    // Exactly one of these is non-null, selected by which constructor was used.
    // Both coupling classes expose an identically-shaped `inCommProcs` of found
    // particles with `.inCell`, `.linearVelocity`, `.lambdaDot`, `.indx`, so the
    // update / write logic is shared via a template over the coupling type.
    FoamYade*        yade_    = nullptr;  // legacy backend
    PgfYadeCoupling* pgfYade_ = nullptr;  // pgf backend

    autoPtr<Function1<scalar>> lambdaFunc_;  // to read lambdaFunction from constant/lambdaDict

    template<class CouplingType>
    void updateImpl(CouplingType& yade);

    template<class CouplingType>
    void writeParticlesDataImpl(CouplingType& yade) const;

public:
    // Legacy backend (FoamYade)
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,   // velocity of spheres
        volVectorField& Us, // interpolated sphere velocity
        volScalarField& porosityF, // porosity field  get from the main solver
        const volScalarField& Ts, // solid temperature (kept for interface compatibility)
        const volScalarField& Ychar, // solid char mass fraction (NEW — drives shrinkage)
        FoamYade& yade
    );

    // PGF-native backend (PgfYadeCoupling)
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,
        volVectorField& Us,
        volScalarField& porosityF,
        const volScalarField& Ts,
        const volScalarField& Ychar,
        PgfYadeCoupling& yade
    );

    void update();
    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank

};

}

#endif // lambdaDotModel_H
