/*
 * Hard set-values Laplacian interpolation of the DEM particle length scale
 * lambda — the scalar counterpart of laplaceSetValues (same for the solid
 * velocity Us).
 *
 * Cells holding particles and cells outside the solid region are imposed as
 * hard constraints on the Laplace matrix via setValues(), and the solve fills
 * the solid cells in between. The interface faces of the solid region are
 * disconnected first so the smoothing cannot reach outside it.
 *
 * The one substantive difference from the velocity version: cells outside the
 * solid region are pinned to lambdaBackgroundValue (default 1.0 m), not zero.
 */

#include "laplaceSetValuesLambda.H"
#include "surfaceInterpolate.H"
#include "fvmDdt.H"
#include "fvmLaplacian.H"
#include "processorPolyPatch.H"

namespace Foam
{

laplaceSetValuesLambda::laplaceSetValuesLambda
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDEM,
    volScalarField& lambda,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const scalar lambdaBackgroundValue
)
:
    LambdaInterpolationModel
    (
        dict,
        mesh,
        lambdaDEM,
        lambda,
        nParticles,
        porosityF,
        lambdaBackgroundValue
    ),
    nLaplaceSetValuesLambdaCorrectors_
    (
        dict.lookupOrDefault<label>("nLaplaceSetValuesLambdaCorrectors", 0)
    )
{}


void laplaceSetValuesLambda::interpolate()
{
    // Cells containing particles already hold the values YADE sent.
    lambda_ = lambdaDEM_;

    // Mask of the solid region (porosityF below criticalPorosity).
    volScalarField whereIs
    (
        IOobject
        (
            "solidLambdaInterpolationWhereIs",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        pos(-(porosityF_ - scalar(solidPorosityCutoff_)))
    );

    // Interpolated to faces so interface faces can be detected: a face with
    // 0 < value < 1 has one solid and one non-solid side.
    surfaceScalarField whereIsPatch = fvc::interpolate(whereIs);

    // Diffusivity of the interpolation Laplacian. Its magnitude does not
    // matter for the converged solution — only the matrix structure does.
    dimensionedScalar coeffD
    (
        "solidLambdaInterpolationDiffusivity",
        dimensionSet(0, 2, 0, 0, 0, 0, 0),
        1e-4
    );

    dimensionedScalar coeffDt
    (
        "solidLambdaInterpolationCorrectorDt",
        dimensionSet(0, 0, 1, 0, 0, 0, 0),
        1e-4
    );

    const dictionary controls(solverControls("lambda"));

    scalar oldInitialResidual = 1e6;
    bool solved = false;
    label keepSolving = 0;

    // Repeat until the residual is small and no longer moving, or 5 attempts.
    while ((keepSolving < 5) && (!solved))
    {
        fvScalarMatrix lambdaLap
        (
            fvm::laplacian(coeffD, lambda_)
        );

        // Disconnect the solid-region interface faces. Only upper() is zeroed:
        // the laplacian matrix is symmetric, so upper() and lower() share one
        // array and zeroing both would flip the matrix to asymmetric.
        forAll(whereIsPatch, faceI)
        {
            if ((whereIsPatch[faceI] > 0) && (whereIsPatch[faceI] < 1))
            {
                lambdaLap.upper()[faceI] = 0.0;
            }
        }

        // Same treatment on processor boundaries, for parallel runs.
        forAll(whereIsPatch.boundaryField(), patchI)
        {
            if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]))
            {
                forAll(whereIsPatch.boundaryField()[patchI], faceI)
                {
                    if
                    (
                        (whereIsPatch.boundaryField()[patchI][faceI] > 0)
                     && (whereIsPatch.boundaryField()[patchI][faceI] < 1)
                    )
                    {
                        lambdaLap.boundaryCoeffs()[patchI][faceI] = 0.0;
                        lambdaLap.internalCoeffs()[patchI][faceI] = 0.0;
                    }
                }
            }
        }

        // Rebuild the diagonal from the modified face coefficients and clear
        // the source.
        lambdaLap.diag() = 0;
        lambdaLap.negSumDiag();
        lambdaLap.source() = 0.0;

        fvScalarMatrix lambdaEqn
        (
            lambdaLap
        );

        List<label> cellList = {};
        Field<scalar> lambdaList = {};

        forAll(lambdaDEM_, cellI)
        {
            // Cells containing particles keep the DEM value.
            if (nParticles_[cellI] > 0.5)
            {
                cellList.append(cellI);
                lambdaList.append(lambdaDEM_[cellI]);
            }
            // Cells outside the solid region hold the background value.
            else if (whereIs[cellI] < 0.5)
            {
                cellList.append(cellI);
                lambdaList.append(lambdaBackgroundValue_);
            }
            // Cells left with a zero diagonal are disconnected from every
            // neighbour; pin them too rather than leave the matrix singular.
            else if (mag(lambdaLap.diag()[cellI]) < SMALL)
            {
                cellList.append(cellI);
                lambdaList.append(lambdaBackgroundValue_);
            }
        }

        const labelUList& cellUList = cellList;
        const Field<scalar>& lambdaUList = lambdaList;

        /*
         * Hard constraints applied before solving:
         *
         * 1. Cells containing DEM particles keep their cell-averaged
         *    per-particle lambda.
         *
         * 2. Cells outside the active solid region hold the background value.
         *
         * 3. Disconnected cells are pinned, to keep the matrix non-singular.
         *
         * This constrains only the lambda interpolation. lambda is diagnostic
         * output in PGF: it feeds no equation, no chemistry and no energy
         * balance.
         */
        lambdaEqn.setValues(cellUList, lambdaUList);

        lambdaEqn.relax();

        // A cell disconnected from all of its neighbours - every face
        // coefficient zeroed at the interface - leaves a zero diagonal, on
        // which symGaussSeidel divides by zero. Pin such a cell instead.
        forAll(lambdaEqn.diag(), cellI)
        {
            scalar& diag = lambdaEqn.diag()[cellI];

            if (!std::isfinite(diag) || mag(diag) < SMALL)
            {
                diag = 1.0;
                lambdaEqn.source()[cellI] = lambdaBackgroundValue_;
                lambda_[cellI] = lambdaBackgroundValue_;
            }
        }

        Foam::SolverPerformance<Foam::scalar> sp = lambdaEqn.solve(controls);

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
    if (solved && nLaplaceSetValuesLambdaCorrectors_ > 0)
    {
        fvScalarMatrix lambdaLap
        (
            fvm::laplacian(coeffD, lambda_)
        );

        forAll(whereIsPatch, faceI)
        {
            if ((whereIsPatch[faceI] > 0) && (whereIsPatch[faceI] < 1))
            {
                lambdaLap.upper()[faceI] = 0.0;
            }
        }

        forAll(whereIsPatch.boundaryField(), patchI)
        {
            if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]))
            {
                forAll(whereIsPatch.boundaryField()[patchI], faceI)
                {
                    if
                    (
                        (whereIsPatch.boundaryField()[patchI][faceI] > 0)
                     && (whereIsPatch.boundaryField()[patchI][faceI] < 1)
                    )
                    {
                        lambdaLap.boundaryCoeffs()[patchI][faceI] = 0.0;
                        lambdaLap.internalCoeffs()[patchI][faceI] = 0.0;
                    }
                }
            }
        }

        lambdaLap.diag() = 0;
        lambdaLap.negSumDiag();
        lambdaLap.source() = 0.0;

        for (label corr = 0; corr < nLaplaceSetValuesLambdaCorrectors_; ++corr)
        {
            fvScalarMatrix lambdaCorrectorEqn
            (
                fvm::ddt(coeffDt, lambda_)
              - lambdaLap
            );

            lambdaCorrectorEqn.solve(controls);
        }
    }

    lambda_.correctBoundaryConditions();
}

} // namespace Foam
