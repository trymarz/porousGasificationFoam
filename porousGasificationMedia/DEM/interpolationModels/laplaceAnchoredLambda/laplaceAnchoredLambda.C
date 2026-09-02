/*
 * Anchored Laplacian interpolation of the DEM particle length scale lambda.
 *
 * Scalar counterpart of laplaceAnchored (which does the same for the solid
 * velocity Us). Cells holding particles are strongly anchored to their raw
 * lambdaDEM value, cells outside the solid region are strongly anchored to the
 * background value, and solid cells without particles are only weakly anchored
 * so the Laplacian term can carry lambda into them from their solid
 * neighbours. After the solve, occupied and non-solid cells are reset exactly
 * to their anchor values.
 *
 * The one substantive difference from the velocity version: the background is
 * lambdaBackgroundValue (default 1.0 m), not zero — a particle size of zero is
 * not a neutral value.
 */

#include "laplaceAnchoredLambda.H"
#include "surfaceInterpolate.H"
#include "fvmSup.H"
#include "fvmLaplacian.H"

namespace Foam
{

laplaceAnchoredLambda::laplaceAnchoredLambda
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

    // Constraint strength holding cells that contain particles close to their
    // raw lambdaDEM value. Larger: occupied cells follow lambdaDEM more
    // strictly. Smaller: occupied-cell lambda is smoothed more by neighbours.
    demLambdaAnchorCoeff_
    (
        dict.lookupOrDefault<scalar>("demLambdaAnchorCoeff", 1e6)
    ),

    // Small regularization for empty solid cells so the interpolation
    // equation stays well posed. Larger: empty solid cells are pulled harder
    // toward the background. Smaller: lambda spreads more freely from the
    // occupied cells.
    backgroundLambdaAnchorCoeff_
    (
        dict.lookupOrDefault<scalar>("backgroundLambdaAnchorCoeff", 1e-12)
    ),

    // Number of times the anchored Laplacian equation is solved.
    nLambdaInterpolationCorrectors_
    (
        dict.lookupOrDefault<label>("nLambdaInterpolationCorrectors", 1)
    )
{}


void laplaceAnchoredLambda::interpolate()
{
    // Mask marking the cells lambda interpolation is allowed to fill.
    volScalarField solidMask
    (
        IOobject
        (
            "solidLambdaInterpolationMask",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    );

    // Anchor strength per cell: how hard each cell is pulled toward its
    // source value. Dimensions 1/m^2 so anchor*lambda matches
    // laplacian(lambda).
    volScalarField anchor
    (
        IOobject
        (
            "solidLambdaInterpolationAnchor",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar
        (
            "zero",
            dimensionSet(0, -2, 0, 0, 0, 0, 0),
            0.0
        )
    );

    // Start from the raw DEM values.
    lambda_ = lambdaDEM_;

    // Classify each cell as occupied, solid-but-empty, or background, and set
    // its anchor strength.
    forAll(lambda_, cellI)
    {
        const bool occupied = nParticles_[cellI] > 0.5;

        const bool solid =
            (porosityF_[cellI] < solidPorosityCutoff_) || occupied;

        // Local length scale squared from the cell volume, so the anchor
        // strength stays comparable across mesh resolutions.
        const scalar length2 =
            max(pow(mesh_.V()[cellI], 2.0/3.0), VSMALL);

        if (solid)
        {
            solidMask[cellI] = 1.0;
        }
        else
        {
            lambda_[cellI] = lambdaBackgroundValue_;
        }

        scalar coeff = backgroundLambdaAnchorCoeff_;

        if (!solid || occupied)
        {
            coeff = demLambdaAnchorCoeff_;
        }

        anchor[cellI] = coeff/length2;
    }

    // Source field the anchor term pulls the solution toward.
    volScalarField sourceLambda
    (
        IOobject
        (
            "lambdaDEMInterpolationSource",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        lambda_
    );

    // Face diffusivity mask: smooth only inside the solid region.
    surfaceScalarField gamma
    (
        IOobject
        (
            "solidLambdaInterpolationGamma",
            mesh_.time().timeName(),
            mesh_,
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
            solidMask[mesh_.owner()[faceI]] < 0.5
         || solidMask[mesh_.neighbour()[faceI]] < 0.5
        )
        {
            gamma[faceI] = 0.0;
        }
    }

    const dictionary controls(solverControls("lambdaDEMInterpolation"));

    for (label corr = 0; corr < nLambdaInterpolationCorrectors_; ++corr)
    {
        fvScalarMatrix lambdaEqn
        (
          - fvm::laplacian(gamma, lambda_)
          + fvm::Sp(anchor, lambda_)
         ==
            anchor*sourceLambda
        );

        lambdaEqn.relax();
        lambdaEqn.solve(controls);
    }

    // Enforce the exact anchor values: occupied cells keep lambdaDEM, non-solid
    // cells keep the background.
    forAll(lambda_, cellI)
    {
        if (nParticles_[cellI] > 0.5)
        {
            lambda_[cellI] = lambdaDEM_[cellI];
        }
        else if (porosityF_[cellI] >= solidPorosityCutoff_)
        {
            lambda_[cellI] = lambdaBackgroundValue_;
        }
    }

    lambda_.correctBoundaryConditions();
}

} // namespace Foam
