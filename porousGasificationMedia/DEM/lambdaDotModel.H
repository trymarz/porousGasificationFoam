#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "DynamicList.H"
#include "Map.H"
#include "autoPtr.H"
#include "fvMesh.H"
#include "FoamYade.H"

namespace Foam
{

template<class Type> class InterpolationModel;

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
    volScalarField& porosityF_;   // from the main solver
    scalar criticalPorosity_;     // from constant/pyrolysisProperties

    FoamYade& yade_;

    // Us from UsDEM: raw per-cell average, smoothed over the solid region.
    Switch interpolateUs_;
    scalar solidPorosityCutoff_;
    autoPtr<InterpolationModel<vector>> usInterpolationModel_;

    // lambda from lambdaDEM, the same way, on its own dictionary keys.
    Switch interpolateLambda_;
    scalar lambdaBackgroundValue_;
    autoPtr<InterpolationModel<scalar>> lambdaInterpolationModel_;

    // Cell-to-cell particle moves since the last exchange.
    Map<label> previousParticleCell_;
    DynamicList<label> movedFromCells_;
    DynamicList<label> movedToCells_;

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

    //- Zero lambdaDot outside the mechanically active solid region before
    //  particles read it (volPyrolysis assembles the value itself).
    void updateLambdaDot();

    //- Pull per-particle state from YADE onto the mesh (count, UsDEM_,
    //  lambdaDEM_), smooth each into its continuous field (Us_, lambda_),
    //  then push lambdaDot_ back out to the particles.
    void updateParticleFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

};

} // namespace Foam

#endif
