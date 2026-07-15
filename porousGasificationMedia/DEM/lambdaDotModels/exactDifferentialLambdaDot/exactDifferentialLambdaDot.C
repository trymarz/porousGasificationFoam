#include "exactDifferentialLambdaDot.H"

namespace Foam
{

exactDifferentialLambdaDot::exactDifferentialLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot),
    // Coefficients are read from the selecting dictionary
    // (constant/solidThermophysicalProperties). Both default to 0.0 when
    // absent, so a case that carries neither key yields lambdaDot = 0.
    dlambdaOverDTs_(dict.lookupOrDefault<scalar>("dlambdaOverDTs", 0.0)),
    massSplitBetweenLamAndPor_
    (
        dict.lookupOrDefault<scalar>("massSplitBetweenLamAndPor", 0.0)
    ),
    TsForLambdaOld_(),
    lambdaDeltaT_(0.0),
    haveTsForLambdaOld_(false)
{
    Info<< "exactDifferentialLambdaDot: dlambdaOverDTs = " << dlambdaOverDTs_
        << ", massSplitBetweenLamAndPor = " << massSplitBetweenLamAndPor_
        << nl << endl;
}


void exactDifferentialLambdaDot::calculateTemperatureDriven()
{
    const volScalarField& TsField =
        mesh_.lookupObject<volScalarField>("Ts");

    const scalarField& Ts = TsField.primitiveField();

    // This runs before Ts is solved for the current step, so oldTime() is not
    // useful here. Compute the rate from the last completed thermal step:
    //     dTsdt = (Ts^n - Ts^(n-1))/deltaT^n
    if (!haveTsForLambdaOld_ || TsForLambdaOld_.size() != Ts.size())
    {
        TsForLambdaOld_ = Ts;
        lambdaDeltaT_ = mesh_.time().deltaTValue();
        haveTsForLambdaOld_ = true;

        // No completed temperature increment exists on the first call.
        lambdaDot_ = 0.0;
        return;
    }

    const scalar dt = max(lambdaDeltaT_, VSMALL);

    forAll(lambdaDot_, cellI)
    {
        const scalar dTsdt = (Ts[cellI] - TsForLambdaOld_[cellI])/dt;
        lambdaDot_[cellI] = dlambdaOverDTs_*dTsdt;
    }

    // Store current Ts for the next call.
    TsForLambdaOld_ = Ts;
    lambdaDeltaT_ = mesh_.time().deltaTValue();
}


void exactDifferentialLambdaDot::calculateChemistryDriven
(
    const volScalarField& sRhoSi,
    const volScalarField& rho,
    const volScalarField& lambda
)
{
    // Chemistry-driven particle shrinkage: massSplitBetweenLamAndPor redirects
    // the volumetric consequence of the specie mass change sRhoSi into the
    // particle length scale, d(lambda)/dYm_i = V_cell/(3*rho*lambda^2) per unit
    // mass rate. The mass change itself stays conserved — the Ym transport in
    // volPyrolysis consumes the full, unmodified sRhoSi.
    forAll(sRhoSi, cellI)
    {
        lambdaDot_[cellI] +=
            massSplitBetweenLamAndPor_*mesh_.V()[cellI]
           /(
                3.0*max(rho[cellI], SMALL)
               *sqr(max(lambda[cellI], SMALL))
            )
           *sRhoSi[cellI];
    }
}

} // namespace Foam
