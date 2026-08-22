#include "lambdaDotModel.H"
#include "UsInterpolationModel.H"
#include "IOdictionary.H"
#include "LambdaDotCalculationModel.H"
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
    volScalarField& porosityF,
    DemYadeCoupler& yade
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    nParticles_(nParticles),
    UsDEM_(UsDEM),
    Us_(Us),
    porosityF_(porosityF),
    yade_(yade),
    interpolateUs_(true),
    solidPorosityCutoff_(1),
    lambdaDotCalculationModel_(nullptr)
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

    // Required entry, shared with UsInterpolationModel: a cell whose porosity
    // reaches criticalPorosity holds too little solid to carry the DEM
    // skeleton, so lambdaDot is switched off there.
    criticalPorosity_ =
        readScalar
        (
            pyrolysisProperties
                .subDict("pyrolysisCoeffs")
                .lookup("criticalPorosity")
        );

    // Select the lambdaDot calculation model from constant/lambdaDict.
    // Valid lambdaMode entries are: constant, Ts, dTsdt.
    lambdaDotCalculationModel_ =
        LambdaDotCalculationModel::New(lambdaDict, mesh_, lambdaDot_);

    // Keep the existing Us interpolation behavior controlled by lambdaDict.
    interpolateUs_ =
        lambdaDict.lookupOrDefault<Switch>("interpolateUs", true);

    solidPorosityCutoff_ =
        lambdaDict.lookupOrDefault<scalar>("solidPorosityCutoff", 1);

    if (interpolateUs_)
    {
        usInterpolationModel_ = UsInterpolationModel::New
        (
            lambdaDict,
            mesh_,
            UsDEM_,
            Us_,
            nParticles_,
            porosityF_
        );
    }
}


void lambdaDotModel::updateLambdaDot()
{
    // Valid lambdaMode entries in constant/lambdaDict are:
    // constant, Ts, dTsdt.
    lambdaDotCalculationModel_->calculate();

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
