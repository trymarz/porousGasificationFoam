#ifndef InterpolationModel_H
#define InterpolationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for smoothing a raw per-cell DEM field (UsDEM,
// lambdaDEM) into a continuous mesh field (Us, lambda) across solid cells
// holding no particles. Templated on Type so one algorithm serves both the
// vector and the scalar field. The per-field differences -- background value
// and New()'s dispatch key -- are arguments rather than specialisations, so
// a case can smooth Us and lambda differently.
template<class Type>
class InterpolationModel
{
protected:
    const fvMesh& mesh_;
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM_;
    GeometricField<Type, fvPatchField, volMesh>& field_;
    const volScalarField& nParticles_;
    const volScalarField& porosityF_;

    //- Porosity at/above which a cell cannot carry the DEM skeleton;
    //  pyrolysisProperties' criticalPorosity, shared with lambdaDotModel.
    scalar solidPorosityCutoff_;

    //- Value held by cells outside the solid region.
    Type backgroundValue_;

    //- Linear-solver controls: the case's fvSolution entry for solverName if
    //  present, else an in-code default, so this internal smoothing step
    //  needs no fvSolution key of its own.
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

    //- Strategy named under modeKey in dict; default laplaceSetValues.
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
