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
    // (constant/lambdaDict). Both default to 0.0 when absent, so a case that
    // carries neither key reproduces the pre-split behaviour (lambdaDot = 0).
    // dlambdaOverDTs is read as a bare scalar (the dicts write it without
    // dimensions) and wrapped in [m/K] here.
    dlambdaOverDTs_
    (
        dimensionedScalar
        (
            "dlambdaOverDTs",
            dimLength/dimTemperature,
            dict.lookupOrDefault<scalar>("dlambdaOverDTs", 0.0)
        )
    ),
    massSplitBetweenLamAndPor_
    (
        dict.lookupOrDefault<scalar>("massSplitBetweenLamAndPor", 0.0)
    ),
    TsForLambdaOld_
    (
        IOobject
        (
            "TsForLambdaOld",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false // unregistered helper field
        ),
        mesh,
        dimensionedScalar("TsOld", dimTemperature, 0.0)
    ),
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

    // This runs before Ts is solved for the current step, so oldTime() is not
    // useful here. Compute the rate from the last completed thermal step:
    //     dTsdt = (Ts^n - Ts^(n-1))/deltaT^n
    if (!haveTsForLambdaOld_ || TsForLambdaOld_.size() != TsField.size())
    {
        TsForLambdaOld_ = TsField;
        lambdaDeltaT_ = mesh_.time().deltaTValue();
        haveTsForLambdaOld_ = true;

        // No completed temperature increment exists on the first call.
        lambdaDot_ = dimensionedScalar("0", dimLength/dimTime, 0.0);
        return;
    }

    const dimensionedScalar dt("dt", dimTime, max(lambdaDeltaT_, VSMALL));

    // Field algebra: dlambdaOverDTs [m/K] * (Ts - TsOld)/dt [K/s] = [m/s].
    // dt must carry dimTime, not a bare scalar, or the division leaves
    // lambdaDot_ with dims [m] instead of [m/s] and this assignment aborts
    // with a dimensionSet mismatch.
    lambdaDot_ = dlambdaOverDTs_*(TsField - TsForLambdaOld_)/dt;

    // Store current Ts for the next call.
    TsForLambdaOld_ = TsField;
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
    // the volumetric consequence of the specie mass rate sRhoSi into the
    // particle length scale, dlambda/dt = V/(3*rho*lambda^2) per unit mass
    // rate (see the class banner for the derivation). With lambda in [m] the
    // term is dimensionally [m/s], matching lambdaDot. Kept cell-wise: mesh_.V()
    // is a scalarField (not a volScalarField) and the per-cell max guards do
    // not map cleanly to field algebra. Accumulates, since the temperature-
    // driven term already set the base value and this is called once per solid
    // specie. The mass change itself stays conserved — the Ym transport in
    // volPyrolysis consumes the full, unmodified sRhoSi, and the porosity
    // source is reduced by the same fraction.
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
