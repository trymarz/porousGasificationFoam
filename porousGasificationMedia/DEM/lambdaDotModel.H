#ifndef lambdaDotModel_H
#define lambdaDotModel_H
#include "DynamicList.H"
#include "Map.H"

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
    word lambdaMode_;  // to switch between single value or function
    scalar lambdaValue_;


    // DasteXar interpolation Us from UsDEM (June 12 2026)
    Switch interpolateUs_;
    word interpolationMode_; // change the mode of interpolation (June 26)
    scalar solidPorosityCutoff_;
    scalar demVelocityAnchorCoeff_;
    scalar backgroundUsAnchorCoeff_;
    label nUsInterpolationCorrectors_;
    label nLaplaceSetValuesCorrectors_;

    void interpolateUsFromDEM();
    void interpolateUsLaplaceAnchored();
    void interpolateUsLaplaceSetValues();
    // Ta inja 

    // to pass values from one cell to another
    Map<label> previousParticleCell_;
    DynamicList<label> movedFromCells_;
    DynamicList<label> movedToCells_;
    //



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

        void updateLambdaDot();
    void updateParticleFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

    void writeParticlesData() const;  // to write ParticlesData.txt in each time step for each rank
};

}

#endif // lambdaDotModel_H
