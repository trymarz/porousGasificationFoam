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

#ifndef laplaceAnchored_H
#define laplaceAnchored_H

#include "InterpolationModel.H"

namespace Foam
{

// Anchored Laplacian interpolation: occupied and non-solid cells are
// strongly anchored to their known/background value; empty solid cells are
// weakly anchored and take their value from neighbours via the Laplacian.
template<class Type>
class LaplaceAnchoredInterpolation
:
    public InterpolationModel<Type>
{
private:
    scalar demAnchorCoeff_;
    scalar backgroundAnchorCoeff_;
    label nInterpolationCorrectors_;

public:
    LaplaceAnchoredInterpolation
    (
        const dictionary& dict,
        const fvMesh& mesh,
        GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
        GeometricField<Type, fvPatchField, volMesh>& field,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const Type& backgroundValue
    );

    virtual ~LaplaceAnchoredInterpolation() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
