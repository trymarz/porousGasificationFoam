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
    // Read as bare scalars (dicts carry no dimensions), wrapped in units here.
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
    // Two accepted spellings, told apart by findDict() returning nullptr for
    // a non-dictionary entry:
    //
    //     dlambdaOverDYmi 1e-4;              // one value for every specie
    //     dlambdaOverDYmi { char 1e-4; }     // per-specie, missing -> 0.0
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

    // Runs before Ts is solved for this step, so oldTime() is not usable; the
    // rate is differenced over the last completed step instead:
    //     dTsdt = (Ts^n - Ts^(n-1))/deltaT^n
    if (!haveTsForLambdaOld_ || TsForLambdaOld_.size() != TsField.size())
    {
        TsForLambdaOld_ = TsField;
        lambdaDeltaT_ = mesh_.time().deltaTValue();
        haveTsForLambdaOld_ = true;

        // No completed increment to difference yet.
        lambdaDot_ = dimensionedScalar("0", dimLength/dimTime, 0.0);
        return;
    }

    const dimensionedScalar dt("dt", dimTime, max(lambdaDeltaT_, VSMALL));

    // [m/K]*[K/s] = [m/s]. dt must carry dimTime, not be a bare scalar, or the
    // result is [m] and the assignment aborts on a dimension mismatch.
    lambdaDot_ = dlambdaOverDTs_*(TsField - TsForLambdaOld_)/dt;

    TsForLambdaOld_ = TsField;
    lambdaDeltaT_ = mesh_.time().deltaTValue();
}


void exactDifferentialLambdaDot::calculateChemistryDriven
(
    const volScalarField& sRhoSi,
    const word& specieName
)
{
    // Accumulates onto the temperature term calculateTemperatureDriven() set.
    // Linear in sRhoSi and independent of rho/lambda/cell volume, so field
    // algebra suffices. sRhoSi < 0 while consuming, so a positive coefficient
    // gives shrinkage.
    const dimensionedScalar dlambdaOverDYmi_i
    (
        "dlambdaOverDYmi(" + specieName + ')',
        dimLength/dimDensity,
        dlambdaOverDYmi(specieName)
    );

    lambdaDot_ += splitMassBetweenLamAndPor_*dlambdaOverDYmi_i*sRhoSi;
}

} // namespace Foam
