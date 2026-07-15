#ifndef DemToFvmMapper_H
#define DemToFvmMapper_H

#include "DynamicList.H"
#include "Map.H"
#include "autoPtr.H"
#include "fvMesh.H"
#include "PgfToYadeMpiCoupler.H"

namespace Foam
{

class UsInterpolationModel;

// Aggregates per-cell particle data from YADE (particle counts, sphere
// velocities, the interpolated Us field) and pushes the cell lambdaDot
// values onto the YADE particles. lambdaDot itself is computed by
// volPyrolysis (dlambdaOverDTs/massSplitBetweenLamAndPor exact-differential split), not here.
class DemToFvmMapper
{
private:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;
    volScalarField& nParticles_;  // number of particles
    volVectorField& UsDEM_;     // velocity of spheres
    volVectorField& Us_;    // interpolated sphere velocity
    volScalarField& porosityF_;   // porosity field  get from the main solver
    PgfToYadeMpiCoupler& pgfToYadeCoupler_;

    // DasteXar interpolation Us from UsDEM (June 12 2026)
    Switch interpolateUs_;
    scalar solidPorosityCutoff_;
    autoPtr<UsInterpolationModel> usInterpolationModel_;
    // Ta inja

    // to pass values from one cell to another
    Map<label> previousParticleCell_;
    DynamicList<label> movedFromCells_;
    DynamicList<label> movedToCells_;
    //

public:
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,   // velocity of spheres
        volVectorField& Us, // interpolated sphere velocity
        volScalarField& porosityF, // porosity field  get from the main solver
        PgfToYadeMpiCoupler& pgfToYadeCoupler
    );

    ~lambdaDotModel();

    void aggregateDemDataIntoFvmFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank
};

} // namespace Foam

#endif
