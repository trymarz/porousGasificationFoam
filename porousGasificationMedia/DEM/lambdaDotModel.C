#include "lambdaDotModel.H"
#include "IOdictionary.H"

namespace Foam
{


lambdaDotModel::lambdaDotModel
(
    const fvMesh& mesh,
    volScalarField& lambdaDot,
    volScalarField& nParticles,
    volVectorField& Us,  // velocity of spheres
    FoamYade& yade
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    nParticles_(nParticles),
    Us_(Us), // velocity of spheres
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
    Us_ = dimensionedVector("zero", Us_.dimensions(), vector::zero);

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = mesh_.findCell(partPtr->pos);
            if (cellI >= 0)
            {
                nParticles_[cellI] += 1.0;

                // sum particle velocities into the cell
                Us_[cellI] += partPtr->linearVelocity;
            }
        }
    }



    // average velocity in  cells containing sphere(s)
    //    empty cells remain zero

    forAll(Us_, cellI)
    {
        if (nParticles_[cellI] > 0.5)
        {
            Us_[cellI] /= nParticles_[cellI];
        }
        else
        {
            Us_[cellI] = vector::zero;
        }
    }

    Us_.correctBoundaryConditions();




    // Assign lambdaDot to particles (only if occupied)

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = mesh_.findCell(partPtr->pos);

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

            label cellI = mesh_.findCell(partPtr->pos);
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
