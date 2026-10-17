#include "apparentVolumeLambdaDot.H"
#include "PstreamReduceOps.H"

namespace Foam
{

apparentVolumeLambdaDot::apparentVolumeLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot),
    minSolidVolume_(dict.lookupOrDefault<scalar>("minSolidVolume", 1e-6)),
    minVolumeRatio_(dict.lookupOrDefault<scalar>("minVolumeRatio", 1e-6)),
    specieNames_(),
    lam3_(),
    Vold_(mesh.nCells(), 0.0),
    dVchem_(mesh.nCells(), 0.0)
{
    if (minSolidVolume_ < 0 || minVolumeRatio_ <= 0)
    {
        FatalIOErrorInFunction(dict)
            << "minSolidVolume must be >= 0 and minVolumeRatio > 0, got "
            << minSolidVolume_ << " and " << minVolumeRatio_
            << exit(FatalIOError);
    }

    // The field carries the dimensionless factor r from here on, 1 = no change;
    // set now so the first exchange, before any chemistry step, sends 1.
    lambdaDot_.dimensions().reset(dimless);
    lambdaDot_ = dimensionedScalar("one", dimless, 1.0);

    Info<< "apparentVolumeLambdaDot: lambdaDot_ is the dimensionless factor r"
        << nl << "    minSolidVolume = " << minSolidVolume_
        << ", minVolumeRatio = " << minVolumeRatio_ << nl << endl;
}


void apparentVolumeLambdaDot::setApparentSpecificVolumes
(
    const wordList& specieNames,
    const scalarList& lam3
)
{
    specieNames_ = specieNames;
    lam3_ = lam3;
}


scalar apparentVolumeLambdaDot::lam3For(const word& specieName) const
{
    forAll(specieNames_, i)
    {
        if (specieNames_[i] == specieName)
        {
            return lam3_[i];
        }
    }

    FatalErrorInFunction
        << "No apparent specific volume for specie '" << specieName
        << "'; setApparentSpecificVolumes() was not called with it."
        << exit(FatalError);

    return 0;
}


void apparentVolumeLambdaDot::calculateTemperatureDriven()
{}


void apparentVolumeLambdaDot::calculateChemistryDriven
(
    const volScalarField& sRhoSi,
    const volScalarField& Ym_i,
    const word& specieName
)
{
    const scalar lam3 = lam3For(specieName);
    const scalar dt = mesh_.time().deltaTValue();

    // Ym_i is still the old value here: the caller solves it after this call.
    Vold_ += lam3*Ym_i.primitiveField();
    dVchem_ += dt*lam3*sRhoSi.primitiveField();
}


void apparentVolumeLambdaDot::endStep()
{
    scalarField& r = lambdaDot_.primitiveFieldRef();
    label nFloored = 0;

    forAll(r, cellI)
    {
        if (Vold_[cellI] < minSolidVolume_)
        {
            r[cellI] = 1.0;
            continue;
        }

        scalar ratio = 1.0 + dVchem_[cellI]/Vold_[cellI];

        if (ratio <= minVolumeRatio_)
        {
            ratio = minVolumeRatio_;
            ++nFloored;
        }

        r[cellI] = cbrt(ratio);
    }

    reduce(nFloored, sumOp<label>());

    if (nFloored)
    {
        WarningInFunction
            << nFloored << " cell(s) at full burnout: volume ratio clamped to "
            << minVolumeRatio_ << endl;
    }

    Vold_ = 0.0;
    dVchem_ = 0.0;
}

} // namespace Foam
