#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "DynamicList.H"
#include "Map.H"
#include "autoPtr.H"
#include "fvMesh.H"
#include "FoamYade.H"
#include "LambdaDotCalculationModel.H"

namespace Foam
{

class UsInterpolationModel;

class lambdaDotModel
{
private:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;
    volScalarField& nParticles_;  // number of particles
    volVectorField& UsDEM_;     // velocity of spheres
    volVectorField& Us_;    // interpolated sphere velocity
    volScalarField& porosityF_;   // porosity field  get from the main solver
    scalar criticalPorosity_; // to read criticalPorosity from constant/pyrolysisProperties
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

    // Runtime-selected lambdaDot calculation model: constant, Ts, or dTsdt.
    autoPtr<LambdaDotCalculationModel> lambdaDotCalculationModel_;

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

    void updateLambdaDot();
    void updateParticleFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank
    void writeVolumeOfSolidArea() const; // to write write solid-region volume 

};

} // namespace Foam

#endif
