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
class LambdaInterpolationModel;

class lambdaDotModel
{
private:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;
    volScalarField& nParticles_;  // number of particles
    volVectorField& UsDEM_;     // velocity of spheres
    volVectorField& Us_;    // interpolated sphere velocity
    volScalarField& lambdaDEM_;   // raw per-cell average of the particles' lambda
    volScalarField& lambda_;      // interpolated particle length scale
    volScalarField& porosityF_;   // porosity field  get from the main solver
    scalar criticalPorosity_; // to read criticalPorosity from constant/pyrolysisProperties
    FoamYade& yade_;

    // DasteXar interpolation Us from UsDEM (June 12 2026)
    Switch interpolateUs_;
    scalar solidPorosityCutoff_;
    autoPtr<UsInterpolationModel> usInterpolationModel_;
    // Ta inja

    // lambda comes back from YADE the same way Us does: raw per-cell average
    // in lambdaDEM_, then smoothed into lambda_ over the solid region.
    Switch interpolateLambda_;
    scalar lambdaBackgroundValue_;
    autoPtr<LambdaInterpolationModel> lambdaInterpolationModel_;

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
        volScalarField& lambdaDEM, // raw per-cell average of particle lambda
        volScalarField& lambda, // interpolated particle length scale
        volScalarField& porosityF, // porosity field  get from the main solver
        FoamYade& yade
    );

    ~lambdaDotModel();

    //- Enforce the solid-region gate on lambdaDot before the DEM particles
    //  read it. The value itself is assembled by volPyrolysis (which owns the
    //  LambdaDotCalculationModel); this only zeroes it outside the
    //  mechanically active solid region.
    void updateLambdaDot();

    //- Pull the per-particle state YADE owns onto the mesh: the per-cell
    //  particle count, the raw solid velocity UsDEM_ and the raw particle
    //  length scale lambdaDEM_, then smooth each into its continuous field
    //  (Us_, lambda_). Finally push lambdaDot_ out to the particles.
    void updateParticleFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank
    void writeVolumeOfSolidArea() const;  // to write the solid-region volume each time step

};

} // namespace Foam

#endif
