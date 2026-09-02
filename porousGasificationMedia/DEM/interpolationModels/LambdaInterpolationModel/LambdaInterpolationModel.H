#ifndef LambdaInterpolationModel_H
#define LambdaInterpolationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for smoothing the raw per-cell DEM particle length scale
// lambdaDEM [m] into the continuous mesh field lambda [m], the scalar
// counterpart of UsInterpolationModel's UsDEM -> Us. YADE owns the integrated
// per-particle lambda (State::lambda_); lambdaDotModel accumulates it into
// lambdaDEM in the cells that hold particles, and this family fills the solid
// cells in between. In PGF lambda is diagnostic output only — nothing in the
// solver computes with it.
//
// Deliberately a parallel implementation rather than a templated
// generalisation of UsInterpolationModel: the concrete Us strategies carry a
// hand-rolled convergence loop, singularity guards and processor-boundary
// handling that would have to be rewritten to templatize safely, and the Us
// path is already shipped and tested.
//
// Two differences from the Us family, both because 0 is not a neutral value
// for a particle size:
//   * cells outside the solid region are pinned to lambdaBackgroundValue
//     (lambdaDict's lambdaBackgroundValue, default 1.0, matching
//     createDEMFields.H's default for the lambda field) rather than to zero;
//   * the dispatch key is lambdaInterpolationMode, so a case can smooth
//     lambda and Us differently within the one lambdaDict.
class LambdaInterpolationModel
{
protected:
    const fvMesh& mesh_;
    volScalarField& lambdaDEM_;
    volScalarField& lambda_;
    const volScalarField& nParticles_;
    const volScalarField& porosityF_;

    //- Porosity at or above which a cell holds too little solid to carry the
    //  DEM skeleton. Read from pyrolysisProperties' criticalPorosity, the
    //  same entry UsInterpolationModel uses.
    scalar solidPorosityCutoff_;

    //- Value held by cells outside the solid region [m].
    scalar lambdaBackgroundValue_;

    //- Linear-solver controls for the interpolation solve under solverName.
    //  Returns the case's system/fvSolution entry when it has one, otherwise
    //  an in-code default. Unlike the Us family, which relies on every DEM
    //  case carrying a "Us" solver entry, lambda interpolation is an internal
    //  smoothing step rather than a physics equation, so it must not make a
    //  case fail to run for want of an fvSolution key it never needed before —
    //  while still being tunable by adding one.
    dictionary solverControls(const word& solverName) const;

public:
    LambdaInterpolationModel
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDEM,
        volScalarField& lambda,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const scalar lambdaBackgroundValue
    );

    virtual ~LambdaInterpolationModel() = default;

    static autoPtr<LambdaInterpolationModel> New
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDEM,
        volScalarField& lambda,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const scalar lambdaBackgroundValue
    );

    virtual void interpolate() = 0;
};

} // namespace Foam

#endif
