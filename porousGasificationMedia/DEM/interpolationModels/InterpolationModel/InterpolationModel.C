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

#include "InterpolationModel.H"
#include "laplaceAnchored.H"
#include "laplaceSetValues.H"
#include "IOdictionary.H"
#include "connectedBelowPorosity.H"

namespace Foam
{

template<class Type>
InterpolationModel<Type>::InterpolationModel
(
    const dictionary& dict,
    const fvMesh& mesh,
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
    GeometricField<Type, fvPatchField, volMesh>& field,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const Type& backgroundValue
)
:
    mesh_(mesh),
    fieldDEM_(fieldDEM),
    field_(field),
    nParticles_(nParticles),
    porosityF_(porosityF),
    solidPorosityCutoff_(0.0),
    backgroundValue_(backgroundValue)
{
    (void)dict;

    // The threshold lives in pyrolysisProperties, not lambdaDict, so both
    // interpolation models and lambdaDotModel agree on where the solid ends.
    IOdictionary pyrolysisProperties
    (
        IOobject
        (
            "pyrolysisProperties",
            mesh.time().constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    solidPorosityCutoff_ =
        connectedBelowPorosity(pyrolysisProperties.subDict("pyrolysisCoeffs"));

    Info<< "connected below porosity = " << solidPorosityCutoff_ << nl << endl;
}


template<class Type>
dictionary InterpolationModel<Type>::solverControls
(
    const word& solverName
) const
{
    if (const dictionary* dictPtr = mesh_.solversDict().findDict(solverName))
    {
        return *dictPtr;
    }

    // Matches the "Us" entry the DEM cases carry.
    dictionary controls;
    controls.add("solver", word("smoothSolver"));
    controls.add("smoother", word("symGaussSeidel"));
    controls.add("tolerance", scalar(1e-6));
    controls.add("relTol", scalar(0.01));

    return controls;
}


template<class Type>
autoPtr<InterpolationModel<Type>> InterpolationModel<Type>::New
(
    const word& modeKey,
    const dictionary& dict,
    const fvMesh& mesh,
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
    GeometricField<Type, fvPatchField, volMesh>& field,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const Type& backgroundValue
)
{
    // laplaceAnchored is a soft anchored diffusion solve, laplaceSetValues a
    // hard setValues Laplace solve. modeKey is an argument so Us and lambda
    // dispatch on their own keys within one lambdaDict.
    const word modelName
    (
        dict.lookupOrDefault<word>(modeKey, "laplaceSetValues")
    );

    Info<< "Selecting " << field.name() << " interpolation model: "
        << modelName << nl << endl;

    if (modelName == "laplaceAnchored")
    {
        return autoPtr<InterpolationModel<Type>>
        (
            new LaplaceAnchoredInterpolation<Type>
            (
                dict,
                mesh,
                fieldDEM,
                field,
                nParticles,
                porosityF,
                backgroundValue
            )
        );
    }
    else if (modelName == "laplaceSetValues")
    {
        return autoPtr<InterpolationModel<Type>>
        (
            new LaplaceSetValuesInterpolation<Type>
            (
                dict,
                mesh,
                fieldDEM,
                field,
                nParticles,
                porosityF,
                backgroundValue
            )
        );
    }

    FatalErrorInFunction
        << "Unknown " << modeKey << " '" << modelName << "'." << nl
        << "Valid options are: laplaceAnchored, laplaceSetValues"
        << exit(FatalError);

    return autoPtr<InterpolationModel<Type>>();
}

} // namespace Foam

// Us (vector) and the particle length scale lambda (scalar).
template class Foam::InterpolationModel<Foam::vector>;
template class Foam::InterpolationModel<Foam::scalar>;
