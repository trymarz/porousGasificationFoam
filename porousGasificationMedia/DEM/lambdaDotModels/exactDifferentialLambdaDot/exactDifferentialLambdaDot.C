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
    // (constant/lambdaDict). All of them default to 0.0 when absent, so a case
    // that carries none of the keys gives lambdaDot = 0 and leaves the whole
    // chemistry mass change in the porosity source. They are read as bare
    // scalars (the dicts write them without dimensions) and wrapped in their
    // physical units where they are used.
    dlambdaOverDTs_
    (
        dimensionedScalar
        (
            "dlambdaOverDTs",
            dimLength/dimTemperature,
            dict.lookupOrDefault<scalar>("dlambdaOverDTs", 0.0)
        )
    ),
    dlambdaOverDYmiUniform_(true),
    dlambdaOverDYmiUniformValue_(0.0),
    dlambdaOverDYmiPerSpecie_(),
    splitMassBetweenLamAndPor_
    (
        dict.lookupOrDefault<scalar>("splitMassBetweenLamAndPor", 0.0)
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
    // dlambdaOverDYmi accepts either form:
    //
    //     dlambdaOverDYmi 1e-4;              // one value for every specie
    //     dlambdaOverDYmi { char 1e-4; }     // per-specie, missing -> 0.0
    //
    // findDict() returns nullptr for a non-dictionary entry, which is what
    // separates the two spellings.
    if (const dictionary* subDictPtr = dict.findDict("dlambdaOverDYmi"))
    {
        dlambdaOverDYmiUniform_ = false;
        dlambdaOverDYmiPerSpecie_ = *subDictPtr;
    }
    else
    {
        dlambdaOverDYmiUniform_ = true;
        dlambdaOverDYmiUniformValue_ =
            dict.lookupOrDefault<scalar>("dlambdaOverDYmi", 0.0);
    }

    Info<< "exactDifferentialLambdaDot: dlambdaOverDTs = " << dlambdaOverDTs_
        << ", splitMassBetweenLamAndPor = " << splitMassBetweenLamAndPor_
        << nl;

    if (dlambdaOverDYmiUniform_)
    {
        Info<< "    dlambdaOverDYmi [m^4/kg] = "
            << dlambdaOverDYmiUniformValue_ << " (all species)" << nl << endl;
    }
    else
    {
        Info<< "    dlambdaOverDYmi [m^4/kg] per specie = "
            << dlambdaOverDYmiPerSpecie_.toc()
            << " (species not listed read 0.0)" << nl << endl;
    }
}


scalar exactDifferentialLambdaDot::dlambdaOverDYmi
(
    const word& specieName
) const
{
    if (dlambdaOverDYmiUniform_)
    {
        return dlambdaOverDYmiUniformValue_;
    }

    return dlambdaOverDYmiPerSpecie_.lookupOrDefault<scalar>(specieName, 0.0);
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
    const word& specieName
)
{
    // One term of the chemistry sum: splitMassBetweenLamAndPor *
    // (dlambda/dYm_i) * (dYm_i/dt), with dYm_i/dt = sRhoSi. Linear in sRhoSi
    // and independent of rho, lambda and the cell volume, so it is plain
    // field algebra rather than the cell-wise loop the earlier geometric form
    // needed. Accumulates: calculateTemperatureDriven() already set the base
    // value, and this is called once per solid specie.
    //
    // Sign: sRhoSi is negative while a specie is being consumed, so a positive
    // dlambdaOverDYmi gives shrinkage (lambdaDot < 0).
    const dimensionedScalar dlambdaOverDYmi_i
    (
        "dlambdaOverDYmi(" + specieName + ')',
        dimLength/dimDensity,
        dlambdaOverDYmi(specieName)
    );

    lambdaDot_ += splitMassBetweenLamAndPor_*dlambdaOverDYmi_i*sRhoSi;
}

} // namespace Foam
