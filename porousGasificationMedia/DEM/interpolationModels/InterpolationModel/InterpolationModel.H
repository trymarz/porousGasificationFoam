#ifndef InterpolationModel_H
#define InterpolationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for smoothing a raw per-cell DEM field (e.g. UsDEM,
// lambdaDEM) into a continuous mesh field (Us, lambda) in the solid cells
// that hold no particles. Templated on the field Type (vector for Us,
// scalar for lambda) so the one algorithm is implemented once instead of
// once per field; replaces the former UsInterpolationModel and
// LambdaInterpolationModel, which duplicated this base for each field.
//
// Two points that differ per field are constructor/factory arguments rather
// than template specialisations:
//   * the background value held by cells outside the solid region (zero for
//     Us, lambdaBackgroundValue for lambda — 0 is not a neutral particle
//     size);
//   * the dictionary key New() dispatches on (interpolationMode for Us,
//     lambdaInterpolationMode for lambda), so a case can smooth the two
//     fields differently within one lambdaDict.
template<class Type>
class InterpolationModel
{
protected:
    const fvMesh& mesh_;
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM_;
    GeometricField<Type, fvPatchField, volMesh>& field_;
    const volScalarField& nParticles_;
    const volScalarField& porosityF_;

    //- Porosity at or above which a cell holds too little solid to carry the
    //  DEM skeleton. Read from pyrolysisProperties' criticalPorosity, shared
    //  with lambdaDotModel so both interpolation models agree with it on
    //  where the solid region ends.
    scalar solidPorosityCutoff_;

    //- Value held by cells outside the solid region.
    Type backgroundValue_;

    //- Linear-solver controls for the interpolation solve under solverName.
    //  Returns the case's system/fvSolution entry when it has one, otherwise
    //  an in-code default, so interpolation is an internal smoothing step
    //  rather than a physics equation and must not make a case fail to run
    //  for want of an fvSolution key it never needed before — while still
    //  being tunable by adding one.
    dictionary solverControls(const word& solverName) const;

public:
    InterpolationModel
    (
        const dictionary& dict,
        const fvMesh& mesh,
        GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
        GeometricField<Type, fvPatchField, volMesh>& field,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const Type& backgroundValue
    );

    virtual ~InterpolationModel() = default;

    //- Select the interpolation strategy named under modeKey in dict
    //  (default laplaceSetValues).
    static autoPtr<InterpolationModel<Type>> New
    (
        const word& modeKey,
        const dictionary& dict,
        const fvMesh& mesh,
        GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
        GeometricField<Type, fvPatchField, volMesh>& field,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const Type& backgroundValue
    );

    virtual void interpolate() = 0;
};

} // namespace Foam

#endif
