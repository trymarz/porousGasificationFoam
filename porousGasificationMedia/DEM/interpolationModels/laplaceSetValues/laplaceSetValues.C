#include "laplaceSetValues.H"
#include "surfaceInterpolate.H"
#include "fvmDdt.H"
#include "fvmLaplacian.H"
#include "processorPolyPatch.H"
// #include "IOdictionary.H"

namespace Foam
{

  // initialize the base interpolation model and read the number of corrector steps (if any).  
laplaceSetValues::laplaceSetValues
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
    nLaplaceSetValuesCorrectors_
    (
        dict.lookupOrDefault<label>("nLaplaceSetValuesCorrectors", 0)
    )
{}

// Interpolate the DEM solid velocity into cells without particles using a laplacian set-values method
void laplaceSetValues::interpolate()
{

    // start from the DEM velocity field
    // cells with particle data already contain known values passed from Yade (UsDEM)
    Us_ = UsDEM_;


    // create a mask to identify the porous (solid porosityF < 1) region based on porosity.
        
      Info<< "laplaceSetValues: criticalPorosity = "
        << solidPorosityCutoff_ << nl << endl;

    volScalarField whereIs
    (
        IOobject
        (
            "solidVelocityInterpolationWhereIs",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        //read the value from constant/pyrolysisProperties : criticalPorosity
            pos(-(porosityF_ - scalar(solidPorosityCutoff_)))
    );

    // Interpolate the porous region mask to cells to detect interface faces.
    surfaceScalarField whereIsPatch = fvc::interpolate(whereIs);


    // diffusion coefficient used in the Laplacian interpolation equation.
    dimensionedScalar coeffD
    (
        "solidVelocityInterpolationDiffusivity",
        dimensionSet(0, 2, 0, 0, 0, 0, 0),
        1e-4
    );

    dimensionedScalar coeffDt
    (
        "solidVelocityInterpolationCorrectorDt",
        dimensionSet(0, 0, 1, 0, 0, 0, 0),
        1e-4
    );
//if (nLaplaceSetValuesCorrectors_ > 0)

// vars used to monitor convergence of the repeated Laplacian solve.
    vector oldInitialResidual = vector(1e6, 1e6, 1e6);
    bool solved = false;
    label keepSolving = 0;

    // repeat the Laplacian solve until the residual is small and stable, or up to 5 attempts.
    while ((keepSolving < 5) && (!solved))
    {

        // build the Laplacian equation that interpolates the Us
        fvVectorMatrix UsLap
        (
            fvm::laplacian(coeffD, Us_)
        );


        forAll(whereIsPatch, faceI)
        {
            if ((whereIsPatch[faceI] > 0) && (whereIsPatch[faceI] < 1))
            {
                UsLap.upper()[faceI] = 0;
            }
        }

         // apply the same interface treatment on processor boundaries for parallel runs.   
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
                        UsLap.boundaryCoeffs()[patchI][faceI] = vector(0, 0, 0);
                        UsLap.internalCoeffs()[patchI][faceI] = vector(0, 0, 0);
                    }
                }
            }
        }

        // rebuild the matrix diagonal and clear the source term after modifying face coefficients
        UsLap.diag() = 0;
        UsLap.negSumDiag();
        UsLap.source() = vector(0, 0, 0);

        // Copy the prepared Laplacian matrix into the equation that will be solved.
        fvVectorMatrix UsEqn
        (
            UsLap
        );

        
        List<label> cellList = {};
        Field<vector> UsList = {};


        // Select cells containing particles and keep their DEM velocity fixed.
        forAll(UsDEM_, cellI)
        {
            if (nParticles_[cellI] > 0.5)
            {
                cellList.append(cellI);
                UsList.append(UsDEM_[cellI]);
            }
        }



        const labelUList& cellUList = cellList;
        const Field<vector>& UsUList = UsList;

        // Force known particle cells to keep their original DEM velocity during interpolation.
        UsEqn.setValues(cellUList, UsUList);

        UsEqn.relax();
        Foam::SolverPerformance<Foam::vector> sp = UsEqn.solve();

        // Print the initial residual to monitor convergence.
        Info << sp.initialResidual() << endl;

        // Mark the solution as converged when the residual is small and no longer changing significantly.
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

    // optional corrector step: run additional pseudo-time smoothing iterations if requested in lambdaDict. 
    // Default nLaplaceSetValuesCorrectors_ is 0 means no corrector steps 
    if (solved && nLaplaceSetValuesCorrectors_ > 0)
    {
        fvVectorMatrix UsLap
        (
            fvm::laplacian(coeffD, Us_)
        );

        forAll(whereIsPatch, faceI)
        {
            if ((whereIsPatch[faceI] > 0) && (whereIsPatch[faceI] < 1))
            {
                UsLap.upper()[faceI] = 0;
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
                        UsLap.boundaryCoeffs()[patchI][faceI] = vector(0, 0, 0);
                        UsLap.internalCoeffs()[patchI][faceI] = vector(0, 0, 0);
                    }
                }
            }
        }



        UsLap.diag() = 0;
        UsLap.negSumDiag();
        UsLap.source() = vector(0, 0, 0);

        for (label corr = 0; corr < nLaplaceSetValuesCorrectors_; ++corr)
        {
            Info << "calculating laplaceSetValues corrector step" << endl;

            fvVectorMatrix UsCorrectorEqn
            (
                fvm::ddt(coeffDt, Us_)
              - UsLap
            );

            UsCorrectorEqn.solve();
        }
    }

    

    Us_.correctBoundaryConditions();
}

} // namespace Foam
