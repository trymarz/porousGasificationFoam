#ifndef DemToFvmMapper_H
#define DemToFvmMapper_H

#include "DynamicList.H"
#include "Map.H"
#include "autoPtr.H"
#include "fvMesh.H"
#include "PgfToYadeMpiCoupler.H"
#include "LambdaDotCalculationModel.H"

namespace Foam
{

class UsInterpolationModel;

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

    // Runtime-selected lambdaDot calculation model: constant, Ts, or dTsdt.
    autoPtr<LambdaDotCalculationModel> lambdaDotCalculationModel_;

public:
    DemToFvmMapper
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,   // velocity of spheres
        volVectorField& Us, // interpolated sphere velocity
        volScalarField& porosityF, // porosity field  get from the main solver
        PgfToYadeMpiCoupler& pgfToYadeCoupler
    );

    ~DemToFvmMapper();

    void updateLambdaDot();
    void aggregateDemDataIntoFvmFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

    void writeParticlesDataToFile() const;  // to write ParticlesData.txt in each time step for each rank
};

} // namespace Foam

#endif
