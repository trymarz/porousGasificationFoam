/*
 * Hard set-values Laplacian interpolation model, generic over Type (vector
 * for Us, scalar for lambda). Cells with particles and cells outside the
 * solid region are hard-pinned via setValues(); the solve fills the gaps
 * between them. Interface faces are disconnected first so smoothing can't
 * reach outside the solid region.
 */

#include "laplaceSetValues.H"
#include "surfaceInterpolate.H"
#include "fvmDdt.H"
#include "fvmLaplacian.H"
#include "processorPolyPatch.H"

namespace Foam
{

// Per-field key names (corrector count; solid-mask field name/write-option).
// Kept distinct rather than templated away: committed cases already set
// nLaplaceSetValuesCorrectors and solidVelocityInterpolationWhereIs under
// these exact names in Us's lambdaDict block; lambda's mask isn't written.
template<class Type>
struct laplaceSetValuesKeys;

template<>
struct laplaceSetValuesKeys<vector>
{
    static word nCorrectors() { return "nLaplaceSetValuesCorrectors"; }
    static word whereIsName() { return "solidVelocityInterpolationWhereIs"; }
    static IOobject::writeOption whereIsWriteOpt()
    {
        return IOobject::AUTO_WRITE;
    }
};

template<>
struct laplaceSetValuesKeys<scalar>
{
    static word nCorrectors() { return "nLaplaceSetValuesLambdaCorrectors"; }
    static word whereIsName() { return "solidLambdaInterpolationWhereIs"; }
    static IOobject::writeOption whereIsWriteOpt()
    {
        return IOobject::NO_WRITE;
    }
};


template<class Type>
LaplaceSetValuesInterpolation<Type>::LaplaceSetValuesInterpolation
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
    nSetValuesCorrectors_
    (
        dict.lookupOrDefault<label>
        (
            laplaceSetValuesKeys<Type>::nCorrectors(), 0
        )
    )
{}


template<class Type>
void LaplaceSetValuesInterpolation<Type>::interpolate()
{
    // Cells containing particles already hold the values YADE sent.
    this->field_ = this->fieldDEM_;

    // Mask of the solid region (porosityF below criticalPorosity).
    volScalarField whereIs
    (
        IOobject
        (
            laplaceSetValuesKeys<Type>::whereIsName(),
            this->mesh_.time().timeName(),
            this->mesh_,
            IOobject::NO_READ,
            laplaceSetValuesKeys<Type>::whereIsWriteOpt()
        ),
        pos(-(this->porosityF_ - scalar(this->solidPorosityCutoff_)))
    );

    // Interpolated to faces so interface faces can be detected: a face with
    // 0 < value < 1 has one solid and one non-solid side.
    surfaceScalarField whereIsPatch = fvc::interpolate(whereIs);

    // Diffusivity of the interpolation Laplacian. Its magnitude does not
    // matter for the converged solution — only the matrix structure does.
    dimensionedScalar coeffD
    (
        "solidInterpolationDiffusivity",
        dimensionSet(0, 2, 0, 0, 0, 0, 0),
        1e-4
    );

    dimensionedScalar coeffDt
    (
        "solidInterpolationCorrectorDt",
        dimensionSet(0, 0, 1, 0, 0, 0, 0),
        1e-4
    );

    const dictionary controls(this->solverControls(this->field_.name()));

    Type oldInitialResidual(1e6*pTraits<Type>::one);
    bool solved = false;
    label keepSolving = 0;

    // Repeat until the residual is small and no longer moving, or 5
    // attempts.
    while ((keepSolving < 5) && (!solved))
    {
        fvMatrix<Type> fieldLap
        (
            fvm::laplacian(coeffD, this->field_)
        );

        // Disconnect solid-region interface faces: only upper() is zeroed --
        // upper()/lower() share storage on this symmetric matrix, so zeroing
        // both would flip it asymmetric.
        forAll(whereIsPatch, faceI)
        {
            if ((whereIsPatch[faceI] > 0) && (whereIsPatch[faceI] < 1))
            {
                fieldLap.upper()[faceI] = 0.0;
            }
        }

        // Same treatment on processor boundaries, for parallel runs.
        forAll(whereIsPatch.boundaryField(), patchI)
        {
            if (isA<processorPolyPatch>(this->mesh_.boundaryMesh()[patchI]))
            {
                forAll(whereIsPatch.boundaryField()[patchI], faceI)
                {
                    if
                    (
                        (whereIsPatch.boundaryField()[patchI][faceI] > 0)
                     && (whereIsPatch.boundaryField()[patchI][faceI] < 1)
                    )
                    {
                        fieldLap.boundaryCoeffs()[patchI][faceI] =
                            pTraits<Type>::zero;
                        fieldLap.internalCoeffs()[patchI][faceI] =
                            pTraits<Type>::zero;
                    }
                }
            }
        }

        // Rebuild the diagonal from the modified coefficients; clear the source.
        fieldLap.diag() = 0;
        fieldLap.negSumDiag();
        fieldLap.source() = pTraits<Type>::zero;

        fvMatrix<Type> fieldEqn
        (
            fieldLap
        );

        List<label> cellList = {};
        Field<Type> valueList = {};

        forAll(this->fieldDEM_, cellI)
        {
            // Cells containing particles keep the DEM value.
            if (this->nParticles_[cellI] > 0.5)
            {
                cellList.append(cellI);
                valueList.append(this->fieldDEM_[cellI]);
            }
            // Cells outside the solid region hold the background value.
            else if (whereIs[cellI] < 0.5)
            {
                cellList.append(cellI);
                valueList.append(this->backgroundValue_);
            }
            // Cells left with a zero diagonal are disconnected from every
            // neighbour; pin them too rather than leave the matrix singular.
            else if (mag(fieldLap.diag()[cellI]) < SMALL)
            {
                cellList.append(cellI);
                valueList.append(this->backgroundValue_);
            }
        }

        const labelUList& cellUList = cellList;
        const Field<Type>& valueUList = valueList;

        // Apply the hard constraints collected above.
        fieldEqn.setValues(cellUList, valueUList);

        fieldEqn.relax();

        // A cell disconnected from every neighbour has a zero diagonal,
        // which symGaussSeidel divides by zero on -- pin it instead.
        forAll(fieldEqn.diag(), cellI)
        {
            scalar& diag = fieldEqn.diag()[cellI];

            if (!std::isfinite(diag) || mag(diag) < SMALL)
            {
                diag = 1.0;
                fieldEqn.source()[cellI] = this->backgroundValue_;
                this->field_[cellI] = this->backgroundValue_;
            }
        }

        SolverPerformance<Type> sp = fieldEqn.solve(controls);

        if
        (
            mag
            (
                mag(oldInitialResidual)
               /max(mag(sp.initialResidual()), VSMALL)
              - 1.0
            ) < 1e-1
         && mag(sp.initialResidual()) < 1e-3
        )
        {
            solved = true;
        }

        oldInitialResidual = sp.initialResidual();

        keepSolving++;
    }

    // Optional pseudo-time smoothing after the set-values solve.
    if (solved && nSetValuesCorrectors_ > 0)
    {
        fvMatrix<Type> fieldLap
        (
            fvm::laplacian(coeffD, this->field_)
        );

        forAll(whereIsPatch, faceI)
        {
            if ((whereIsPatch[faceI] > 0) && (whereIsPatch[faceI] < 1))
            {
                fieldLap.upper()[faceI] = 0.0;
            }
        }

        forAll(whereIsPatch.boundaryField(), patchI)
        {
            if (isA<processorPolyPatch>(this->mesh_.boundaryMesh()[patchI]))
            {
                forAll(whereIsPatch.boundaryField()[patchI], faceI)
                {
                    if
                    (
                        (whereIsPatch.boundaryField()[patchI][faceI] > 0)
                     && (whereIsPatch.boundaryField()[patchI][faceI] < 1)
                    )
                    {
                        fieldLap.boundaryCoeffs()[patchI][faceI] =
                            pTraits<Type>::zero;
                        fieldLap.internalCoeffs()[patchI][faceI] =
                            pTraits<Type>::zero;
                    }
                }
            }
        }

        fieldLap.diag() = 0;
        fieldLap.negSumDiag();
        fieldLap.source() = pTraits<Type>::zero;

        for (label corr = 0; corr < nSetValuesCorrectors_; ++corr)
        {
            fvMatrix<Type> fieldCorrectorEqn
            (
                fvm::ddt(coeffDt, this->field_)
              - fieldLap
            );

            fieldCorrectorEqn.solve(controls);
        }
    }

    this->field_.correctBoundaryConditions();
}

} // namespace Foam

template class Foam::LaplaceSetValuesInterpolation<Foam::vector>;
template class Foam::LaplaceSetValuesInterpolation<Foam::scalar>;
