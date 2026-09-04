/*
 * Anchored Laplacian interpolation model, generic over Type (vector for Us,
 * scalar for lambda). Occupied cells are strongly anchored to their raw DEM
 * value, non-solid cells to the background value (lambdaBackgroundValue for
 * lambda -- 0 is not a neutral particle size), and empty solid cells only
 * weakly, so the Laplacian carries the value in from solid neighbours.
 * Occupied/non-solid cells are reset exactly to their anchor after solving.
 */

#include "laplaceAnchored.H"
#include "surfaceInterpolate.H"
#include "fvmSup.H"
#include "fvmLaplacian.H"

namespace Foam
{

// Per-field anchor-coefficient key names. Kept distinct rather than
// templated away: committed cases already set demVelocityAnchorCoeff /
// backgroundUsAnchorCoeff / nUsInterpolationCorrectors under these exact
// names in Us's lambdaDict block.
template<class Type>
struct laplaceAnchoredKeys;

template<>
struct laplaceAnchoredKeys<vector>
{
    static word demCoeff() { return "demVelocityAnchorCoeff"; }
    static word backgroundCoeff() { return "backgroundUsAnchorCoeff"; }
    static word nCorrectors() { return "nUsInterpolationCorrectors"; }
};

template<>
struct laplaceAnchoredKeys<scalar>
{
    static word demCoeff() { return "demLambdaAnchorCoeff"; }
    static word backgroundCoeff() { return "backgroundLambdaAnchorCoeff"; }
    static word nCorrectors() { return "nLambdaInterpolationCorrectors"; }
};


template<class Type>
LaplaceAnchoredInterpolation<Type>::LaplaceAnchoredInterpolation
(
    const dictionary& dict,
    const fvMesh& mesh,
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
    GeometricField<Type, fvPatchField, volMesh>& field,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const Type& backgroundValue
)
:
    InterpolationModel<Type>
    (
        dict,
        mesh,
        fieldDEM,
        field,
        nParticles,
        porosityF,
        backgroundValue
    ),

    // Constraint strength holding occupied cells to their raw DEM value --
    // larger follows it more strictly, smaller lets neighbours smooth it more.
    demAnchorCoeff_
    (
        dict.lookupOrDefault<scalar>
        (
            laplaceAnchoredKeys<Type>::demCoeff(), 1e6
        )
    ),

    // Small regularization keeping empty solid cells well posed. Their anchor
    // source is the raw DEM field, which is zero where no particle sits, so
    // this pulls them toward zero -- not toward backgroundValue_, which only
    // non-solid cells hold. Larger pulls harder, smaller spreads occupied
    // values more freely; at the 1e-12 default the Laplacian dominates.
    backgroundAnchorCoeff_
    (
        dict.lookupOrDefault<scalar>
        (
            laplaceAnchoredKeys<Type>::backgroundCoeff(), 1e-12
        )
    ),

    // Number of times the anchored Laplacian equation is solved.
    nInterpolationCorrectors_
    (
        dict.lookupOrDefault<label>
        (
            laplaceAnchoredKeys<Type>::nCorrectors(), 1
        )
    )
{}


template<class Type>
void LaplaceAnchoredInterpolation<Type>::interpolate()
{
    typedef GeometricField<Type, fvPatchField, volMesh> FieldType;

    // Mask marking the cells interpolation is allowed to fill.
    volScalarField solidMask
    (
        IOobject
        (
            "solidInterpolationMask",
            this->mesh_.time().timeName(),
            this->mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        this->mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    );

    // Anchor strength per cell: how hard each cell is pulled toward its
    // source value. Dimensions 1/m^2 so anchor*field matches
    // laplacian(field).
    volScalarField anchor
    (
        IOobject
        (
            "solidInterpolationAnchor",
            this->mesh_.time().timeName(),
            this->mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        this->mesh_,
        dimensionedScalar
        (
            "zero",
            dimensionSet(0, -2, 0, 0, 0, 0, 0),
            0.0
        )
    );

    // Start from the raw DEM values.
    this->field_ = this->fieldDEM_;

    // Classify each cell as occupied, solid-but-empty, or background, and
    // set its anchor strength.
    forAll(this->field_, cellI)
    {
        const bool occupied = this->nParticles_[cellI] > 0.5;

        const bool solid =
            (this->porosityF_[cellI] < this->solidPorosityCutoff_)
         || occupied;

        // Local length scale squared from the cell volume, so the anchor
        // strength stays comparable across mesh resolutions.
        const scalar length2 =
            max(pow(this->mesh_.V()[cellI], 2.0/3.0), VSMALL);

        if (solid)
        {
            solidMask[cellI] = 1.0;
        }
        else
        {
            this->field_[cellI] = this->backgroundValue_;
        }

        scalar coeff = backgroundAnchorCoeff_;

        if (!solid || occupied)
        {
            coeff = demAnchorCoeff_;
        }

        anchor[cellI] = coeff/length2;
    }

    // Source field the anchor term pulls the solution toward.
    FieldType source
    (
        IOobject
        (
            "DEMInterpolationSource",
            this->mesh_.time().timeName(),
            this->mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        this->field_
    );

    // Face diffusivity mask: smooth only inside the solid region.
    surfaceScalarField gamma
    (
        IOobject
        (
            "solidInterpolationGamma",
            this->mesh_.time().timeName(),
            this->mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        fvc::interpolate(solidMask)
    );

    // Disable smoothing across faces touching a non-solid cell.
    forAll(gamma, faceI)
    {
        if
        (
            solidMask[this->mesh_.owner()[faceI]] < 0.5
         || solidMask[this->mesh_.neighbour()[faceI]] < 0.5
        )
        {
            gamma[faceI] = 0.0;
        }
    }

    const dictionary controls(this->solverControls(this->field_.name()));

    for (label corr = 0; corr < nInterpolationCorrectors_; ++corr)
    {
        fvMatrix<Type> fieldEqn
        (
          - fvm::laplacian(gamma, this->field_)
          + fvm::Sp(anchor, this->field_)
         ==
            anchor*source
        );

        fieldEqn.relax();
        fieldEqn.solve(controls);
    }

    // Enforce the exact anchor values: occupied cells keep the raw DEM
    // value, non-solid cells keep the background.
    forAll(this->field_, cellI)
    {
        if (this->nParticles_[cellI] > 0.5)
        {
            this->field_[cellI] = this->fieldDEM_[cellI];
        }
        else if (this->porosityF_[cellI] >= this->solidPorosityCutoff_)
        {
            this->field_[cellI] = this->backgroundValue_;
        }
    }

    this->field_.correctBoundaryConditions();
}

} // namespace Foam

template class Foam::LaplaceAnchoredInterpolation<Foam::vector>;
template class Foam::LaplaceAnchoredInterpolation<Foam::scalar>;
