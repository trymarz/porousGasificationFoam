/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | www.openfoam.com
     \\/     M anipulation  |
-------------------------------------------------------------------------------
    Copyright (C) 2021 OpenCFD Ltd.
    Copyright (C) YEAR AUTHOR,AFFILIATION
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

#include "codedFunction1Template.H"
#include "addToRunTimeSelectionTable.H"
#include "unitConversion.H"

//{{{ begin codeInclude

//}}} end codeInclude

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * * Local Functions * * * * * * * * * * * * * * //

//{{{ begin localCode

//}}} end localCode

// * * * * * * * * * * * * * * * Global Functions  * * * * * * * * * * * * * //

// dynamicCode:
// SHA1 = 28bfc523e58f0c73f4d6c26df812b3af7a4198c3
//
// unique function name that can be checked if the correct library version
// has been loaded
extern "C" void lambdaDot_28bfc523e58f0c73f4d6c26df812b3af7a4198c3(bool load)
{
    if (load)
    {
        // Code that can be explicitly executed after loading
    }
    else
    {
        // Code that can be explicitly executed before unloading
    }
}


namespace Function1Types
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

//makeFunction1(lambdaDotFunction1, scalar);
defineTypeNameAndDebug
(
    lambdaDotFunction1_scalar,
    0
);
Function1<scalar>::addRemovabledictionaryConstructorToTable
    <lambdaDotFunction1_scalar>
    addRemovablelambdaDotFunction1_scalarConstructorToTable_;

} // namespace Function1Types
} // namespace Foam


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::Function1Types::
lambdaDotFunction1_scalar::
lambdaDotFunction1_scalar
(
    const word& entryName,
    const dictionary& dict,
    const objectRegistry* obrPtr
)
:
    Function1<scalar>(entryName, dict, obrPtr)
{
    if (false)
    {
        printMessage("Construct lambdaDot Function1 from dictionary");
    }
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::scalar
Foam::Function1Types::lambdaDotFunction1_scalar::value
(
    const scalar x
) const
{
//{{{ begin code
    #line 14 "/home/tr/KVM-share/Code/github.com/trymarz/porousGasificationFoam/tutorials/cases/DEM_UsInterp_solidU/constant/lambdaDict/lambdaDot"
return 0.1*sin(x);
//}}} end code
}


// ************************************************************************* //

