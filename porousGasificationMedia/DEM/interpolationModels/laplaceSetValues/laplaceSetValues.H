#ifndef laplaceSetValues_H
#define laplaceSetValues_H

#include "InterpolationModel.H"

namespace Foam
{

// Hard set-values Laplacian interpolation: cells holding particles and cells
// outside the solid region are imposed as hard constraints on the Laplace
// matrix via setValues(), and the solve fills the solid cells in between.
// Templated over Type so the one algorithm serves both the vector solid
// velocity (Us) and the scalar DEM particle length scale (lambda) — see
// InterpolationModel.H.
template<class Type>
class LaplaceSetValuesInterpolation
:
    public InterpolationModel<Type>
{
private:
    label nSetValuesCorrectors_;

public:
    LaplaceSetValuesInterpolation
    (
        const dictionary& dict,
        const fvMesh& mesh,
        GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
        GeometricField<Type, fvPatchField, volMesh>& field,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const Type& backgroundValue
    );

    virtual ~LaplaceSetValuesInterpolation() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
