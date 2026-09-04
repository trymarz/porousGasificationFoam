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
    // Coefficients default to 0.0 when absent (unconfigured -> lambdaDot = 0,
    // all mass to porosity). Read as bare scalars (dicts have no dimensions)
    // and wrapped in their physical units here.
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

    // dlambdaOverDTs [m/K] * (Ts - TsOld)/dt [K/s] = [m/s]. dt must carry
    // dimTime, not a bare scalar, or lambdaDot_ ends up [m] and this
    // assignment aborts on a dimensionSet mismatch.
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
    // One term of splitMassBetweenLamAndPor * (dlambda/dYm_i) * sRhoSi,
    // accumulated per specie onto the base value calculateTemperatureDriven()
    // already set. Linear in sRhoSi and independent of rho/lambda/cell
    // volume, so plain field algebra suffices (no cell-wise loop). Sign:
    // sRhoSi < 0 while consuming, so a positive coefficient gives shrinkage.
    const dimensionedScalar dlambdaOverDYmi_i
    (
        "dlambdaOverDYmi(" + specieName + ')',
        dimLength/dimDensity,
        dlambdaOverDYmi(specieName)
    );

    lambdaDot_ += splitMassBetweenLamAndPor_*dlambdaOverDYmi_i*sRhoSi;
}

} // namespace Foam
