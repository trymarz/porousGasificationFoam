#include "lambdaDotModel.H"
#include "InterpolationModel.H"
#include "IOdictionary.H"
#include "PstreamReduceOps.H"
#include "OSspecific.H"

#include <fstream>

namespace Foam
{

lambdaDotModel::lambdaDotModel
(
    const fvMesh& mesh,
    volScalarField& lambdaDot,
    volScalarField& nParticles,
    volVectorField& UsDEM,
    volVectorField& Us,
    volScalarField& lambdaDEM,
    volScalarField& lambda,
    volScalarField& porosityF,
    FoamYade& yade
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    nParticles_(nParticles),
    UsDEM_(UsDEM),
    Us_(Us),
    lambdaDEM_(lambdaDEM),
    lambda_(lambda),
    porosityF_(porosityF),
    yade_(yade),
    interpolateUs_(true),
    solidPorosityCutoff_(1),
    interpolateLambda_(true),
    lambdaBackgroundValue_(1.0)
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

    IOdictionary pyrolysisProperties
    (
        IOobject
        (
            "pyrolysisProperties",
            mesh.time().constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    // Required entry, shared with InterpolationModel<vector>: a cell whose
    // porosity reaches criticalPorosity holds too little solid to carry the
    // DEM skeleton, so lambdaDot is switched off there.
    criticalPorosity_ =
        readScalar
        (
            pyrolysisProperties
                .subDict("pyrolysisCoeffs")
                .lookup("criticalPorosity")
        );

    // Keep the existing Us interpolation behavior controlled by lambdaDict.
    interpolateUs_ =
        lambdaDict.lookupOrDefault<Switch>("interpolateUs", true);

    solidPorosityCutoff_ =
        lambdaDict.lookupOrDefault<scalar>("solidPorosityCutoff", 1);

    if (interpolateUs_)
    {
        usInterpolationModel_ = InterpolationModel<vector>::New
        (
            "interpolationMode",
            lambdaDict,
            mesh_,
            UsDEM_,
            Us_,
            nParticles_,
            porosityF_,
            pTraits<vector>::zero
        );
    }

    // Same pattern for lambda, on its own keys so a case can smooth lambda
    // and Us independently. lambdaBackgroundValue is read here rather than
    // inside the interpolation model because the non-interpolating branch of
    // updateParticleFields() needs it too, and that branch never constructs a
    // model.
    interpolateLambda_ =
        lambdaDict.lookupOrDefault<Switch>("interpolateLambda", true);

    lambdaBackgroundValue_ =
        lambdaDict.lookupOrDefault<scalar>("lambdaBackgroundValue", 1.0);

    if (interpolateLambda_)
    {
        lambdaInterpolationModel_ = InterpolationModel<scalar>::New
        (
            "lambdaInterpolationMode",
            lambdaDict,
            mesh_,
            lambdaDEM_,
            lambda_,
            nParticles_,
            porosityF_,
            lambdaBackgroundValue_
        );
    }
}


void lambdaDotModel::updateLambdaDot()
{
    // lambdaDot itself is assembled by volPyrolysis::solveSpeciesMass(), which
    // owns the LambdaDotCalculationModel selected by lambdaMode in
    // constant/lambdaDict (constant | exactDifferential). That runs inside
    // pyrolysisZone.evolve(), i.e. after this call in the solver loop, so the
    // value gated and pushed to the particles here is the one assembled by the
    // previous time step.
    //
    // lambdaDot drives deformation of the DEM solid skeleton and must not
    // remain active in cells outside the mechanically active solid region.
    forAll(lambdaDot_, cellI)
    {
        if (porosityF_[cellI] >= criticalPorosity_)
        {
            lambdaDot_[cellI] = 0.0;
        }
    }

    lambdaDot_.correctBoundaryConditions();
}


lambdaDotModel::~lambdaDotModel() = default;

void lambdaDotModel::updateParticleFields()
{
    // count particles per cell, and sum the per-particle state YADE owns
    // (velocity, length scale) per cell
    nParticles_ = 0.0;
    UsDEM_ = dimensionedVector("zero", UsDEM_.dimensions(), vector::zero);
    lambdaDEM_ = dimensionedScalar("zero", lambdaDEM_.dimensions(), 0.0);

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

            // sum the particles' integrated length scale into the cell. YADE
            // owns this value (State::lambda_, advanced every DEM step from
            // the lambdaDot PGF sends it); PGF only reads it back.
            lambdaDEM_[cellI] += partPtr->lambda;
        }
    }

    // average velocity and length scale in cells containing sphere(s).
    // Empty cells are left at zero: both are raw accumulators, meaningless
    // where no particle contributed. The continuous fields built from them
    // (Us_, lambda_) are what carry a defined value everywhere.
    forAll(UsDEM_, cellI)
    {
        if (nParticles_[cellI] > 0.5)
        {
            UsDEM_[cellI] /= nParticles_[cellI];
            lambdaDEM_[cellI] /= nParticles_[cellI];
        }
        else
        {
            UsDEM_[cellI] = vector::zero;
            lambdaDEM_[cellI] = 0.0;
        }
    }

    UsDEM_.correctBoundaryConditions();
    lambdaDEM_.correctBoundaryConditions();

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
        usInterpolationModel_->interpolate();
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

    // Same for lambda: either smooth lambdaDEM over the solid region, or take
    // it raw with the non-solid cells held at the background value. lambda is
    // diagnostic output in PGF — no equation reads it — so this only affects
    // what gets written.
    if (interpolateLambda_)
    {
        lambdaInterpolationModel_->interpolate();
    }
    else
    {
        forAll(lambda_, cellI)
        {
            if (nParticles_[cellI] > 0.5)
            {
                lambda_[cellI] = lambdaDEM_[cellI];
            }
            else
            {
                lambda_[cellI] = lambdaBackgroundValue_;
            }
        }

        lambda_.correctBoundaryConditions();
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

// Report, at write times only, the two integrals over the solid region
// (cells with porosityF below criticalPorosity): its total volume, and its
// volume-weighted average porosity.
void lambdaDotModel::writeVolumeOfSolidArea() const
{
    if (!mesh_.time().outputTime()) return;

    scalar localVolume = 0.0;
    scalar localPorosityVolume = 0.0;

    const scalarField& cellVolumes = mesh_.V();

    // Use exactly the same cells for both calculations.
    forAll(porosityF_, cellI)
    {
        if (porosityF_[cellI] < criticalPorosity_)
        {
            localVolume += cellVolumes[cellI];

            localPorosityVolume +=
                porosityF_[cellI] * cellVolumes[cellI];
        }
    }

    // Combine contributions from all MPI processors.
    scalar totalVolume = localVolume;
    scalar totalPorosityVolume = localPorosityVolume;

    reduce(totalVolume, sumOp<scalar>());
    reduce(totalPorosityVolume, sumOp<scalar>());

    // Only the master processor writes the global results.
    if (!Pstream::master())
    {
        return;
    }

    scalar averagePorosity = 0.0;

    if (totalVolume > VSMALL)
    {
        averagePorosity =
            totalPorosityVolume / totalVolume;
    }

    const fileName casePath
    (
        mesh_.time().rootPath()
      / mesh_.time().globalCaseName()
    );

    const fileName postProcessingDir
    (
        casePath / "postProcessing"
    );

    const fileName outputDir
    (
        postProcessingDir / "solidAreaVolume"
    );

    mkDir(postProcessingDir);
    mkDir(outputDir);


    // ---------------------------------------------------------
    // Write solidAreaVolume.dat
    // ---------------------------------------------------------

    const fileName volumeOutputFile
    (
        outputDir / "solidAreaVolume.dat"
    );

    const bool writeVolumeHeader = !isFile(volumeOutputFile);

    std::ofstream volumeOfs
    (
        volumeOutputFile.c_str(),
        std::ios::out | std::ios::app
    );

    if (writeVolumeHeader)
    {
        volumeOfs << "# Time\tVolumeOfSolidArea\n";
        volumeOfs << "# criticalPorosity = "
                  << criticalPorosity_ << "\n";
    }

    volumeOfs.setf(std::ios::scientific);
    volumeOfs.precision(12);

    // timeName() gives exactly the same time name used
    // for the OpenFOAM write-time directory.
    volumeOfs << mesh_.time().timeName()
              << "\t"
              << totalVolume
              << "\n";


    // ---------------------------------------------------------
    // Write avgPorosity.dat
    // ---------------------------------------------------------

    const fileName porosityOutputFile
    (
        outputDir / "avgPorosity.dat"
    );

    const bool writePorosityHeader = !isFile(porosityOutputFile);

    std::ofstream porosityOfs
    (
        porosityOutputFile.c_str(),
        std::ios::out | std::ios::app
    );

    if (writePorosityHeader)
    {
        porosityOfs << "# Time\tAveragePorosity\n";
        porosityOfs << "# Selected cells: porosityF < criticalPorosity\n";
        porosityOfs << "# criticalPorosity = "
                    << criticalPorosity_ << "\n";
        porosityOfs << "# Average is volume-weighted\n";
    }

    porosityOfs.setf(std::ios::scientific);
    porosityOfs.precision(12);

    porosityOfs << mesh_.time().timeName()
                << "\t"
                << averagePorosity
                << "\n";
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
