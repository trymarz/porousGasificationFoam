
// Laplacian interpolation model with soft anchors for known DEM velocity cells and zero-velocity background cells.

/*
 * Anchored Laplacian solid velocity interpolation model.
 *
 * This model interpolates the DEM-based solid velocity field into cells without
 * particles by solving a Laplacian smoothing equation with source anchors.
 * Occupied cells are strongly anchored to their DEM velocity, non-solid cells
 * are strongly anchored to zero velocity, and solid cells without particles are
 * weakly anchored while receiving velocity information from neighboring solid
 * cells through the Laplacian term. After the solve, occupied cells are reset
 * exactly to UsDEM and non-solid cells are reset to zero.
 
 */

#include "laplaceAnchored.H"
#include "surfaceInterpolate.H"
#include "fvmSup.H"
#include "fvmLaplacian.H"

namespace Foam
{

    // initialize the base interpolation model and read anchor/corrector parameters from the dictionary. constant/lambdaDict

laplaceAnchored::laplaceAnchored
(
    const dictionary& dict,
    const fvMesh& mesh,
    volVectorField& UsDEM,
    volVectorField& Us,
    const volScalarField& nParticles,
    const volScalarField& porosityF
)
:
    UsInterpolationModel
    (
        dict,
        mesh,
        UsDEM,
        Us,
        nParticles,
        porosityF
    ),

    // Coefficient for strongly forcing occupied cells to remain close to DEM velocity values.
    // constraint strength that keeps cells with sphere centers close to their raw UsDEM value. Larger value: occupied cells follow UsDEM more strictly. Smaller value: occupied-cell velocity is smoothed more by neighbors.
        //Larger demVelocityAnchorCoeff strengthens the influence of DEM cells.
    demVelocityAnchorCoeff_
    (
        dict.lookupOrDefault<scalar>("demVelocityAnchorCoeff", 1e6)
    ),

    // Small background coefficient for weakly anchoring interpolation in solid cells without particles.
    // small regularization for empty solid cells so the interpolation equation remains well posed. Larger value: empty solid cells are weakly pulled toward zero. Smaller value: interpolation spreads more freely from DEM cells.
    backgroundUsAnchorCoeff_
    (
        dict.lookupOrDefault<scalar>("backgroundUsAnchorCoeff", 1e-12)
    ),

    // Number of times the anchored Laplacian equation is solved.
    nUsInterpolationCorrectors_
    (
        dict.lookupOrDefault<label>("nUsInterpolationCorrectors", 1)
    )
{}


// Interpolate DEM solid velocity using a Laplacian equation with soft source anchors.
void laplaceAnchored::interpolate()
{
    // Mask field marking cells where solid velocity interpolation is allowed.
    volScalarField solidMask
    (
        IOobject
        (
            "solidVelocityInterpolationMask",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    );

    // Anchor-strength field controlling how strongly each cell is forced toward its source velocity.
    volScalarField anchor
    (
        IOobject
        (
            "solidVelocityInterpolationAnchor",
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

    // Start from the DEM velocity field before applying interpolation.
    Us_ = UsDEM_;


    // Classify each cell as  occupied, solid, or gas ( background) and assign its anchor strength.
    forAll(Us_, cellI)
    {
        // A cell is occupied when it contains at least one DEM particle.
        const bool occupied = nParticles_[cellI] > 0.5;

        // A cell is treated as solid if porosity is below the cutoff or if it contains particles.
        const bool solid =
            (porosityF_[cellI] < solidPorosityCutoff_) || occupied;

            // Estimate a local length scale squared from the cell volume to scale the anchor coefficient.
            // scale the anchor strength with cell size so the interpolation behavior stays more consistent when the mesh resolution changes.
        const scalar length2 =
            max(pow(mesh_.V()[cellI], 2.0/3.0), VSMALL);


        // force non-solid cells to zero velocity.
        if (solid)
        {
            solidMask[cellI] = 1.0;
        }
        else
        {
            Us_[cellI] = vector::zero;
        }

        scalar coeff = backgroundUsAnchorCoeff_;

        if (!solid || occupied)
        {
            coeff = demVelocityAnchorCoeff_;
        }

        anchor[cellI] = coeff/length2;
    }

    // Source velocity field toward which the anchor term pulls the solution.
    volVectorField sourceUs
    (
        IOobject
        (
            "UsDEMInterpolationSource",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        Us_
    );

    // Face diffusivity mask: allows Laplacian smoothing only inside the solid region.
    surfaceScalarField gamma
    (
        IOobject
        (
            "solidVelocityInterpolationGamma",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        fvc::interpolate(solidMask)
    );

    // Disable interpolation across faces connected to non-solid cells.
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

    // Solve the anchored Laplacian equation for the requested number of corrector iterations.

    for (label corr = 0; corr < nUsInterpolationCorrectors_; ++corr)
    {

        // Build the interpolation equation: Laplacian smoothing plus anchor forcing.

        fvVectorMatrix UsEqn
        (
          - fvm::laplacian(gamma, Us_)
          + fvm::Sp(anchor, Us_)
         ==
            anchor * sourceUs
        );

        UsEqn.relax();
        UsEqn.solve("UsDEMInterpolation");
    }

    // Enforce exact final values: occupied cells keep DEM velocity and non-solid cells remain zero.

    forAll(Us_, cellI)
    {
        if (nParticles_[cellI] > 0.5)
        {
            Us_[cellI] = UsDEM_[cellI];
        }
        else if (porosityF_[cellI] >= solidPorosityCutoff_)
        {
            Us_[cellI] = vector::zero;
        }
    }


    Us_.correctBoundaryConditions();
}

} // namespace Foam
