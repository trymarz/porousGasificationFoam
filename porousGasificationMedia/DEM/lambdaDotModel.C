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
    const volScalarField& Ychar, // solid char mass fraction
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
    Ychar_(Ychar),
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
    // ── Ychar-driven lambdaDot ──────────────────────────────────────
    //
    // lambdaDot = 1.0 − Ychar   (clamped to [0.0, 1.0])
    //
    // Ychar is the *solid char mass fraction* char/(wood+char), which runs
    // from 0 (all wood) to 1 (all char) independently of the chemistry
    // stoichiometry — so lambdaDot is simply the wood-remaining fraction.
    // The stoichiometric char yield (0.70 char for posterDemo, 0.30 for
    // posterDemo-coldBed) only sets how *fast* Ychar climbs, hence how
    // gradually the spheres shrink; it does not enter this formula.
    //
    //   Ychar = 0.00  →  lambdaDot = 1.00  →  r = r₀ = 0.004 m
    //   Ychar = 0.50  →  lambdaDot = 0.50  →  r = 0.0025 m (halfway)
    //   Ychar = 1.00  →  lambdaDot = 0.00  →  r = r_core = 0.001 m
    //
    // The YADE changeRadius() function (MPI_lambda.py) maps ld to
    // radius linearly:  r = r_core + (r₀ − r_core) × ld
    //
    // The legacy lambdaDict / lambdaFunc_ is still read for backward
    // compatibility but its value is intentionally unused, as is Ts_.
    // lambdaDot_.correctBoundaryConditions() is called after the loop
    // so boundary patches get consistent values.

    forAll(lambdaDot_, cellI)
    {
        const scalar yc = Ychar_[cellI];

        // Guard against unphysical Ychar (should be [0, 1] but advection
        // overshoot can push it a few percent beyond).
        const scalar ycClipped = max(0.0, min(1.0, yc));

        lambdaDot_[cellI] = 1.0 - ycClipped;
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
