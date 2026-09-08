#include "lambdaDotModel.H"
#include "InterpolationModel.H"
#include "IOdictionary.H"

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

    // Required entry, shared with the interpolation models: at or above
    // criticalPorosity a cell holds too little solid to carry the DEM
    // skeleton, so lambdaDot is switched off there.
    criticalPorosity_ =
        readScalar
        (
            pyrolysisProperties
                .subDict("pyrolysisCoeffs")
                .lookup("criticalPorosity")
        );

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

    // On their own keys, so a case can smooth lambda and Us differently.
    // Read here rather than in the model: the non-interpolating branch of
    // updateParticleFields() needs them too.
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
    // lambdaDot drives deformation of the DEM skeleton, so it must not stay
    // active outside the solid region. volPyrolysis::solveSpeciesMass()
    // assembles it later in the step, so the value gated and pushed to
    // particles here is the previous step's.
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

            // Length scale, owned by YADE (State::lambda_, advanced every
            // DEM step from the lambdaDot PGF sends); PGF only reads it back.
            lambdaDEM_[cellI] += partPtr->lambda;
        }
    }

    // Average over occupied cells; empty cells stay zero. Both are raw
    // accumulators -- Us_/lambda_ carry the defined value everywhere.
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

    // Same for lambda, raw cells falling back to the background value. No
    // PGF equation reads lambda; it is diagnostic output.
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

    // Push lambdaDot back to the particles in occupied cells.
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


const DynamicList<label>& lambdaDotModel::movedFromCells() const
{
    return movedFromCells_;
}


const DynamicList<label>& lambdaDotModel::movedToCells() const
{
    return movedToCells_;
}

} // namespace Foam
