#include "lambdaDotModel.H"
#include "UsInterpolationModel.H"
#include "IOdictionary.H"

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
    solidPorosityCutoff_(1),

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

    lambdaMode_ =
        lambdaDict.lookupOrDefault<word>("lambdaMode", "constant");

    lambdaValue_ =
        lambdaDict.lookupOrDefault<scalar>("lambdaValue", 0.0);

    //DasteXar interpolation
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

    // ta inja
}


lambdaDotModel::~lambdaDotModel() = default;


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
