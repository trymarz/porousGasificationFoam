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

Class
    Foam::heterogeneousPyrolysisModels::SolidFields

Description
    References to the solid-phase thermo, chemistry and fields that
    volPyrolysis owns and that several of its stages share. volPyrolysis
    builds one in createCollaborators() and hands it to each stage, which
    binds the members it uses; the bundle itself is not kept.

    A field only one stage needs stays an explicit constructor argument of
    that stage, so the argument list shows what is particular to it.

\*---------------------------------------------------------------------------*/

#ifndef solidFields_H
#define solidFields_H

#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "HGSSolidThermo.H"
#include "porousThermoSolidChemistryModel.H"
#include "solidState.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                       Struct SolidFields Declaration
\*---------------------------------------------------------------------------*/

struct SolidFields
{
    //- Mesh the solid fields live on
    const fvMesh& mesh;

    //- Solid thermo; owns T, rho and Ys
    HGSSolidThermo& thermo;

    //- Solid chemistry; reaction rates and the gas/solid tables
    const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry;

    //- Per-cell solid regime classification
    SolidState& state;

    //- Solid temperature [K]
    volScalarField& T;

    //- True solid density [kg/m3]
    volScalarField& rho;

    //- Solid mass fractions [-]
    PtrList<volScalarField>& Ys;

    //- Solid species bulk densities [kg/m3]
    PtrList<volScalarField>& Ym;

    //- Solid enthalpy density [J/m3]
    volScalarField& solidH;

    //- Gas volume fraction [-]
    volScalarField& porosity;

    //- Solid velocity [m/s]
    const volVectorField& Us;

    //- Limited solid volumetric face flux [m3/s]
    const surfaceScalarField& phiSolid;

    //- Whether pyrolysis is active; follows the owner's switch
    const Switch& active;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
