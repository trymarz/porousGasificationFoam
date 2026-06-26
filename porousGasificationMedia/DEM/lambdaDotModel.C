#include "surfaceInterpolate.H"
#include "fvmSup.H"
#include "fvmDdt.H"
#include "processorPolyPatch.H"
#include "lambdaDotModel.H"
#include "IOdictionary.H"
#include "fvmLaplacian.H"


namespace Foam
{

lambdaDotModel::lambdaDotModel
(
    const fvMesh& mesh,
    volScalarField& lambdaDot,
    volScalarField& nParticles,
    volVectorField& UsDEM,  // velocity of spheres
    volVectorField& Us, // interpolated velocity of spheres
    volScalarField& porosityF,
    FoamYade& yade
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    nParticles_(nParticles),
    UsDEM_(UsDEM), // velocity of spheres
    Us_(Us),
    porosityF_(porosityF),
    yade_(yade),
    lambdaMode_("constant"),
    lambdaValue_(0.0),

    //DasteXar for interpolation of UsDEM into Us
    interpolateUs_(true),
    interpolationMode_("laplaceSetValues"), // primary logic

    solidPorosityCutoff_(1),
    demVelocityAnchorCoeff_(1e6),
    backgroundUsAnchorCoeff_(1e-12),
    nUsInterpolationCorrectors_(1),
    nLaplaceSetValuesCorrectors_(0),



    // to read lambda function from constant/lambdaDict
    lambdaFunc_
    (
        Function1<scalar>::New
        (
            "lambdaDot",
            IOdictionary
            (
                IOobject
                (
                    "lambdaDict",
                    mesh.time().constant(),
                    mesh,
                    IOobject::MUST_READ,
                    IOobject::NO_WRITE
                )
            ),
            &mesh
        )
    )

{
    IOdictionary lambdaDict
    (
        IOobject
        (
            "lambdaDict",
            mesh.time().constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    lambdaMode_ = lambdaDict.lookupOrDefault<word>("lambdaMode", "constant");
    lambdaValue_ = lambdaDict.lookupOrDefault<scalar>("lambdaValue", 0.0);


    //DasteXar interpolation
    interpolateUs_ =
        lambdaDict.lookupOrDefault<Switch>("interpolateUs", true);


    // Select the UsDEM-to-Us interpolation method:
    // laplaceAnchored keeps the current soft anchored diffusion solve,
    // laplaceSetValues uses the pgfVeloInt hard setValues Laplace solve.


    interpolationMode_ =
        lambdaDict.lookupOrDefault<word>("laplaceAnchored", "interpolationMode");

    solidPorosityCutoff_ =
        lambdaDict.lookupOrDefault<scalar>("solidPorosityCutoff", 1);

    demVelocityAnchorCoeff_ =
        lambdaDict.lookupOrDefault<scalar>("demVelocityAnchorCoeff", 1e6);

    backgroundUsAnchorCoeff_ =
        lambdaDict.lookupOrDefault<scalar>("backgroundUsAnchorCoeff", 1e-12);

    nUsInterpolationCorrectors_ =
        lambdaDict.lookupOrDefault<label>("nUsInterpolationCorrectors", 1);

    nLaplaceSetValuesCorrectors_ =
        lambdaDict.lookupOrDefault<label>("nLaplaceSetValuesCorrectors", 0);

    // ta inja
}


void lambdaDotModel::updateLambdaDot()
{
    const volScalarField* TsPtr = nullptr;

    if (lambdaMode_ == "Ts")
    {
        TsPtr = &mesh_.lookupObject<volScalarField>("Ts");
    }

    forAll(lambdaDot_, cellI)
    {
        if (lambdaMode_ == "constant")
        {
            lambdaDot_[cellI] = lambdaValue_;
        }
        else if (lambdaMode_ == "Ts")
        {
            lambdaDot_[cellI] = lambdaFunc_->value((*TsPtr)[cellI]);
        }
        else
        {
            FatalErrorInFunction
                << "Unknown lambdaMode '" << lambdaMode_
                << "'. Valid options are: constant, Ts"
                << exit(FatalError);
        }
    }

    lambdaDot_.correctBoundaryConditions();
}

void lambdaDotModel::updateParticleFields()
{
    // count particles per cell
    // and sum velocities per cell
    nParticles_ = 0.0;
    UsDEM_ = dimensionedVector("zero", UsDEM_.dimensions(), vector::zero);

    movedFromCells_.clear();
    movedToCells_.clear();

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = partPtr->inCell;
            if (cellI < 0) continue;




                        const label particleI = partPtr->indx;

            if (previousParticleCell_.found(particleI))
            {
                const label oldCellI = previousParticleCell_[particleI];

                if (oldCellI >= 0 && oldCellI != cellI)
                {
                    movedFromCells_.append(oldCellI);
                    movedToCells_.append(cellI);
                }
            }

            previousParticleCell_.set(particleI, cellI);






            nParticles_[cellI] += 1.0;

            // sum particle velocities into the cell
            UsDEM_[cellI] += partPtr->linearVelocity;
        }
    }

    // average velocity in cells containing sphere(s)
    // empty cells remain zero

    forAll(UsDEM_, cellI)
    {
        if (nParticles_[cellI] > 0.5)
        {
            UsDEM_[cellI] /= nParticles_[cellI];
        }
        else
        {
            UsDEM_[cellI] = vector::zero;
        }
    }

    UsDEM_.correctBoundaryConditions();

    // Raw DEM velocity in occupied cells. Keep empty cells at zero while
    // validating the coupled data path; broad smoothing can destabilize
    // solid species/porosity transport before the DEM velocity is limited.
    // Us_ = UsDEM_;

    // forAll(Us_, cellI)
    // {
    //     if (porosityF_[cellI] >= 0.999)
    //     {
    //         Us_[cellI] = vector::zero;
    //     }
    // }


        if (interpolateUs_)
    {
        interpolateUsFromDEM();
    }
    else
    {
        Us_ = UsDEM_;

        forAll(Us_, cellI)
        {
            if (porosityF_[cellI] >= solidPorosityCutoff_)
            {
                Us_[cellI] = vector::zero;
            }
        }

        Us_.correctBoundaryConditions();
    }



    // Assign lambdaDot to particles (only if occupied)

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = partPtr->inCell;
            if (cellI < 0) continue;
            if (nParticles_[cellI] < 0.5) continue;

            partPtr->lambdaDot = lambdaDot_[cellI];
        }
    }
}



//DasteXar interpolation

void lambdaDotModel::interpolateUsFromDEM()
{
    if (interpolationMode_ == "laplaceAnchored")
    {
        interpolateUsLaplaceAnchored();
    }
    else if (interpolationMode_ == "laplaceSetValues")
    {
        interpolateUsLaplaceSetValues();
    }
    else
    {
        FatalErrorInFunction
            << "Unknown interpolationMode '" << interpolationMode_
            << "'. Valid options are: laplaceAnchored, laplaceSetValues"
            << exit(FatalError);
    }
}



void lambdaDotModel::interpolateUsLaplaceAnchored()

{
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

    Us_ = UsDEM_;

    forAll(Us_, cellI)
    {
        const bool occupied = nParticles_[cellI] > 0.5;
        const bool solid = (porosityF_[cellI] < solidPorosityCutoff_) || occupied;

        const scalar length2 = max(pow(mesh_.V()[cellI], 2.0/3.0), VSMALL);

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

    for (label corr = 0; corr < nUsInterpolationCorrectors_; ++corr)
    {
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


void lambdaDotModel::interpolateUsLaplaceSetValues()
{
    Us_ = UsDEM_;

    volScalarField whereIs
    (
        IOobject
        (
            "solidVelocityInterpolationWhereIs",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        pos(-(porosityF_ - scalar(1.0)))
    );

    surfaceScalarField whereIsPatch = fvc::interpolate(whereIs);

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

    vector oldInitialResidual = vector(1e6, 1e6, 1e6);
    bool solved = false;
    label keepSolving = 0;

    while ((keepSolving < 5) && (!solved))
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

        fvVectorMatrix UsEqn
        (
            UsLap
        );

        List<label> cellList = {};
        Field<vector> UsList = {};

        forAll(UsDEM_, cellI)
        {
            if (mag(UsDEM_[cellI]) > 0)
            {
                cellList.append(cellI);
                UsList.append(UsDEM_[cellI]);
            }
        }

        const labelUList& cellUList = cellList;
        const Field<vector>& UsUList = UsList;

        UsEqn.setValues(cellUList, UsUList);

        UsEqn.relax();
        Foam::SolverPerformance<Foam::vector> sp = UsEqn.solve();

        Info << sp.initialResidual() << endl;

        if
        (
            mag(mag(oldInitialResidual)/max(mag(sp.initialResidual()), VSMALL) - 1.0) < 1e-1
         && mag(sp.initialResidual()) < 1e-3
        )
        {
            solved = true;
        }

        oldInitialResidual = sp.initialResidual();

        keepSolving++;
    }

    if (nLaplaceSetValuesCorrectors_ > 0)
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


//-------------------------





void lambdaDotModel::writeParticlesData() const
{
    if (!mesh_.time().outputTime()) return;

    const int myRank = Pstream::myProcNo();

    fileName outDir = mesh_.time().timePath();
    mkDir(outDir);

    fileName outPath = outDir / "ParticlesData.txt";

    std::ofstream ofs(outPath.c_str(), std::ios::app);
    ofs.setf(std::ios::scientific);
    ofs.precision(8);

    ofs << "# rank " << myRank
        << " time " << mesh_.time().timeName()
        << " (particleID cellID lambdaDot)\n";

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = partPtr->inCell;
            if (cellI < 0) continue;
            if (nParticles_[cellI] < 0.5) continue;

            ofs << partPtr->indx << " "
                << cellI << " "
                << lambdaDot_[cellI] << "\n";
        }
    }

    ofs << "\n";
}


const DynamicList<label>& lambdaDotModel::movedFromCells() const
{
    return movedFromCells_;
}


const DynamicList<label>& lambdaDotModel::movedToCells() const
{
    return movedToCells_;
}


} // namespace Foam
