#ifndef laplaceAnchored_H
#define laplaceAnchored_H

#include "InterpolationModel.H"

namespace Foam
{

// Anchored Laplacian interpolation: occupied cells and non-solid cells are
// strongly anchored to their known/background value, solid cells without
// particles are weakly anchored while receiving the value from neighbours
// through the Laplacian term. Templated over Type so the one algorithm
// serves both the vector solid velocity (Us) and the scalar DEM particle
// length scale (lambda) — see InterpolationModel.H.
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
