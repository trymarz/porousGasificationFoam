/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

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


void apparentVolumeLambdaDot::setApparentDensities
(
    const wordList& specieNames,
    const scalarList& rhoApparent
)
{
    // rhoApparent is optional in solidThermophysicalProperties (0 if absent)
    // and required here: without it the particle volume is unknown.
    specieNames_ = specieNames;
    lam3_.setSize(rhoApparent.size());

    forAll(rhoApparent, i)
    {
        if (rhoApparent[i] <= 0)
        {
            FatalErrorInFunction
                << "lambdaMode apparentVolume needs rhoApparent [kg/m^3] for "
                << "every solid specie; '" << specieNames[i] << "' has none."
                << nl << "Add it to " << specieNames[i] << "Coeffs/density in "
                << "constant/solidThermophysicalProperties."
                << exit(FatalError);
        }

        lam3_[i] = 1.0/rhoApparent[i];
    }
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
        << "'; setApparentDensities() was not called with it."
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
