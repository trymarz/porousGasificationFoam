#ifndef laplaceSetValues_H
#define laplaceSetValues_H

#include "InterpolationModel.H"

namespace Foam
{

// Hard set-values Laplacian interpolation: cells holding particles and cells
// outside the solid region are hard-pinned via setValues(); the solve fills
// the gaps between them. Templated over Type to serve both Us (vector) and
// lambda (scalar) -- see InterpolationModel.H.
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
