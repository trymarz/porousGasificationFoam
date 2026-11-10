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

#ifndef exactDifferentialLambdaDot_H
#define exactDifferentialLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "volFields.H"

namespace Foam
{

// lambdaDot: the rate of change of the DEM particle length scale lambda [m],
// written as a total derivative in solid temperature Ts and specie mass
// concentrations Ym_i:
//
//   dlambda/dt = dlambdaOverDTs*(dTs/dt) + sum_i[dlambdaOverDYmi_i*sRhoSi_i]
//
// Both coefficients come from constant/lambdaDict, take any sign, and default
// to 0.0, so an unconfigured case gives lambdaDot = 0:
//
//   dlambdaOverDTs   [m/K]
//   dlambdaOverDYmi  [m^4/kg]  one scalar, or a per-specie subdict
//
// dlambdaOverDYmi is dimLength/dimDensity, so [m^4/kg]*[kg/m^3/s] = [m/s].
//
// lambda and porosity are separate state variables, and each responds to the
// full chemistry rate. Porosity is 1 - Vsolid/Vfvm, and Vsolid follows from
// Ym_i and rhos (which never depends on Ym_i), so every unit of mass consumed
// moves porosity by the same amount. How far lambda moves for that same mass
// is set by dlambdaOverDYmi alone, unrelated in size and sign to porosity.
//
// Picture a cell as a 10x10 grid of solid or fluid compartments: porosity
// counts the fluid ones, lambda measures the box around the solid ones, which
// depends on where they sit rather than how many. The picture is descriptive
// only -- the solver stores one lambda and one porosity per cell, with no
// structure inside it.
//
// beginStep() (base class) zeroes lambdaDot_; calculateTemperatureDriven()
// then adds the temperature term; calculateChemistryDriven() adds each
// specie's chemistry term.
class exactDifferentialLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    //- Temperature coefficient dlambda/dTs [m/K].
    dimensionedScalar dlambdaOverDTs_;

    //- True when dlambdaOverDYmi is one scalar for every specie, false when
    //  it is a per-specie subdict.
    bool dlambdaOverDYmiUniform_;

    //- The single dlambdaOverDYmi value [m^4/kg], used when uniform.
    scalar dlambdaOverDYmiUniformValue_;

    //- Per-specie dlambdaOverDYmi values [m^4/kg], keyed by the bare
    //  solidComponents name. A specie absent from the subdict reads 0.0.
    dictionary dlambdaOverDYmiPerSpecie_;

    //- Ts sampled at this point of the previous step, which dTs/dt is
    //  differenced against; the flag is false until the first call fills it.
    volScalarField TsForLambdaOld_;
    bool haveTsForLambdaOld_;

    //- Resolve dlambdaOverDYmi [m^4/kg] for one specie.
    scalar dlambdaOverDYmi(const word& specieName) const;

public:
    exactDifferentialLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~exactDifferentialLambdaDot() = default;

    virtual void calculateTemperatureDriven() override;

    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const word& specieName
    ) override;
};

} // namespace Foam

#endif
