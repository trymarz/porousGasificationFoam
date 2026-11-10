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

#ifndef LambdaDotCalculationModel_H
#define LambdaDotCalculationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for the DEM particle length-scale rate lambdaDot [m/s].
// Owned by volPyrolysis, selected via lambdaMode in constant/lambdaDict, and
// driven in SolidSpeciesTransport::solve(): beginStep() once per step, then
// calculateTemperatureDriven() once, then calculateChemistryDriven() per
// solid specie, then endStep() once. PGF owns only what it sends YADE: the
// rate [m/s] (YADE integrates State::lambda_), or the factor under
// apparentVolume, applied once per exchange.
class LambdaDotCalculationModel
{
protected:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;

public:
    LambdaDotCalculationModel
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~LambdaDotCalculationModel() = default;

    static autoPtr<LambdaDotCalculationModel> New
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    //- Zeroes lambdaDot_. Call once per step, before
    //  calculateTemperatureDriven() and calculateChemistryDriven(), which
    //  both add onto it with +=. Not virtual: the reset is the same for
    //  every model, so no calculate*Driven() override needs to double as it.
    void beginStep();

    //- Adds the temperature-driven contribution to lambdaDot_. Called once
    //  per step, before the species loop.
    virtual void calculateTemperatureDriven() = 0;

    //- Adds this specie's mass-rate sRhoSi [kg/m^3/s] contribution to
    //  lambdaDot_. specieName is the bare solidComponents name ("char", not
    //  "Ychar"), keying the per-specie coefficient. Default no-op.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const word& specieName
    )
    {}

    //- As above, also given this specie's Ym_i [kg/m^3], not yet solved this
    //  step. Default forwards to the two-argument form.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const volScalarField& Ym_i,
        const word& specieName
    )
    {
        calculateChemistryDriven(sRhoSi, specieName);
    }

    //- Called once after the species loop. Default no-op.
    virtual void endStep()
    {}

    //- Called once after construction with each solidComponents name and
    //  its rhoApparent [kg/m^3], 0 where not given. Default no-op.
    virtual void setApparentDensities
    (
        const wordList& specieNames,
        const scalarList& rhoApparent
    )
    {}
};

} // namespace Foam

#endif
