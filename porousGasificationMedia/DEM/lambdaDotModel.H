#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "DynamicList.H"
#include "Map.H"
#include "autoPtr.H"
#include "fvMesh.H"
#include "FoamYade.H"

namespace Foam
{

class UsInterpolationModel;

// Aggregates per-cell particle data from YADE (particle counts, sphere
// velocities, the interpolated Us field) and pushes the cell lambdaDot
// values onto the YADE particles. lambdaDot itself is computed by
// volPyrolysis via the selected LambdaDotCalculationModel (lambdaMode in
// constant/solidThermophysicalProperties), not here.
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
        FoamYade& yade
    );

    ~lambdaDotModel();

    void updateParticleFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank
};

} // namespace Foam

#endif
