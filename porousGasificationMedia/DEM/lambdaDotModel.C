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
    // and Us independently. Read here (not inside the interpolation model)
    // because updateParticleFields()'s non-interpolating branch needs it too.
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
    // lambdaDot is assembled by volPyrolysis::solveSpeciesMass() (via
    // pyrolysisZone.evolve(), which runs after this call each time step), so
    // the value gated and pushed to particles here is last step's value.
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

    // Average velocity/length scale in occupied cells; empty cells stay
    // zero -- both are raw accumulators, meaningless with no particle
    // contribution. Us_/lambda_ are what carry a defined value everywhere.
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

    // Same for lambda: smooth lambdaDEM, or take it raw with non-solid cells
    // at the background value. Diagnostic output only -- no equation reads it.
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


const DynamicList<label>& lambdaDotModel::movedFromCells() const
{
    return movedFromCells_;
}


const DynamicList<label>& lambdaDotModel::movedToCells() const
{
    return movedToCells_;
}

} // namespace Foam
