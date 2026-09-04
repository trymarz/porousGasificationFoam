#ifndef laplaceAnchored_H
#define laplaceAnchored_H

#include "InterpolationModel.H"

namespace Foam
{

// Anchored Laplacian interpolation: occupied and non-solid cells are
// strongly anchored to their known/background value; empty solid cells are
// weakly anchored, receiving their value from neighbours via the Laplacian
// term. Templated over Type to serve both Us (vector) and lambda (scalar)
// -- see InterpolationModel.H.
template<class Type>
class LaplaceAnchoredInterpolation
:
    public InterpolationModel<Type>
{
private:
    scalar demAnchorCoeff_;
    scalar backgroundAnchorCoeff_;
    label nInterpolationCorrectors_;

public:
    LaplaceAnchoredInterpolation
    (
        const dictionary& dict,
        const fvMesh& mesh,
        GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
        GeometricField<Type, fvPatchField, volMesh>& field,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const Type& backgroundValue
    );

    virtual ~LaplaceAnchoredInterpolation() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
