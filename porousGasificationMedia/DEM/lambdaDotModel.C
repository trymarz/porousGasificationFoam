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
    forAll(lambdaDot_, cellI)
    {
        // direct function
        //const point& c = mesh_.C()[cellI];
        //lambdaDot_[cellI] = 0.1 * Foam::sin(c.x());

        // to read function from constant/lambdaDict
        const point& c = mesh_.C()[cellI];
        lambdaDot_[cellI] = lambdaFunc_->value(c.y());
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
            if (cellI < 0) continue;

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
    Us_.correctBoundaryConditions();

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

} // namespace Foam
