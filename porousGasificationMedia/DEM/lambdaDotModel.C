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
    const volScalarField& Ts, // solid temperature
    FoamYade& yade
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    nParticles_(nParticles),
    UsDEM_(UsDEM), // velocity of spheres
    Us_(Us),
    porosityF_(porosityF),
    Ts_(Ts),
    yade_(yade),
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
{}


void lambdaDotModel::update()
{
    // calc lambdaDot field
    //
    // lambdaDot is the per-cell radius-scaling factor sent back to YADE: a
    // value < 1 shrinks the spheres in that cell each coupling step. Here it
    // is driven by the local solid temperature Ts: cells below Tref do not
    // shrink (lambdaDot = 1), and shrinkage ramps linearly up to a maximum
    // rate as Ts rises from Tref to Tactive. This realises the PGF -> YADE
    // half of the coupling (hot solid devolatilises and the particles shrink
    // toward their char core). The legacy lambdaDict / lambdaFunc_ is still
    // read for backward compatibility but its value is intentionally unused.
    const scalar Tref            = 500.0;    // below this, no shrinkage
    const scalar Tactive         = 800.0;    // at this, maximum shrink rate
    const scalar maxShrinkPerStep = 0.005;   // lambdaDot = 0.995 at Tactive (10× faster)

    forAll(lambdaDot_, cellI)
    {
        const scalar TsLocal = Ts_[cellI];

        if (TsLocal <= Tref)
        {
            lambdaDot_[cellI] = 1.0;
        }
        else
        {
            const scalar frac =
                min(1.0, (TsLocal - Tref) / (Tactive - Tref));
            lambdaDot_[cellI] = 1.0 - frac * maxShrinkPerStep;
        }
    }

    lambdaDot_.correctBoundaryConditions();

    // count particles per cell
    // and sum velocities per cell
    nParticles_ = 0.0;
    UsDEM_ = dimensionedVector("zero", UsDEM_.dimensions(), vector::zero);

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = partPtr->inCell;
            if (cellI < 0 || cellI >= mesh_.nCells()) continue;

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

    // interpolated velocity of spheres for cells withough sphere but containing solid matterial

    Us_ = UsDEM_;

    forAll(Us_, cellI)
    {
        if (porosityF_[cellI] >= 0.999)
        {
            Us_[cellI] = vector::zero;
        }
    }

    fvVectorMatrix UsEqn
    (
        fvm::laplacian
            (
                dimensionedScalar("one", dimless, 1.0),
                Us_
            )
    );

    UsEqn.solve();

    forAll(Us_, cellI)
    {
        if (porosityF_[cellI] >= 0.999)
        {
            Us_[cellI] = vector::zero;
        }
    }


    // Assign lambdaDot to particles (only if occupied)

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = partPtr->inCell;
            if (cellI < 0 || cellI >= mesh_.nCells()) continue;
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
            if (cellI < 0 || cellI >= mesh_.nCells()) continue;
            if (nParticles_[cellI] < 0.5) continue;

            ofs << partPtr->indx << " "
                << cellI << " "
                << lambdaDot_[cellI] << "\n";
        }
    }

    ofs << "\n";
}

} // namespace Foam
