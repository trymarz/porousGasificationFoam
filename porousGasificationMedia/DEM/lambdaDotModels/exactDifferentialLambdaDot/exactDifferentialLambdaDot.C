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

    // Called after SolidEnergy::solve(), so TsField is this step's Ts and
    // dTsdt = (Ts^n - Ts^(n-1))/deltaT^n. The previous sample is kept here:
    // Ts.oldTime() is not maintained on the equilibrium path.
    if (!haveTsForLambdaOld_ || TsForLambdaOld_.size() != TsField.size())
    {
        TsForLambdaOld_ = TsField;
        haveTsForLambdaOld_ = true;

        // No completed increment to difference yet: nothing to add.
        return;
    }

    // deltaT of the step just taken: runTime++ advanced the clock by it before
    // this step's Ts was solved, so it is the interval the increment accrued
    // over. Identical to the previous step's deltaT whenever the time step is
    // fixed, which a DEM-coupled run requires anyway.
    const dimensionedScalar dt
    (
        "dt",
        dimTime,
        max(mesh_.time().deltaTValue(), VSMALL)
    );

    // [m/K]*[K/s] = [m/s]; dt carries dimTime so the result is a rate, not a
    // length.
    lambdaDot_ += dlambdaOverDTs_*(TsField - TsForLambdaOld_)/dt;

    TsForLambdaOld_ = TsField;
}


void exactDifferentialLambdaDot::calculateChemistryDriven
(
    const volScalarField& sRhoSi,
    const word& specieName
)
{
    // Linear in sRhoSi, so it needs no cell-volume weighting. sRhoSi < 0
    // while mass is consumed, so a positive coefficient shrinks the particle.
    const dimensionedScalar dlambdaOverDYmi_i
    (
        "dlambdaOverDYmi(" + specieName + ')',
        dimLength/dimDensity,
        dlambdaOverDYmi(specieName)
    );

    lambdaDot_ += dlambdaOverDYmi_i*sRhoSi;
}

} // namespace Foam
