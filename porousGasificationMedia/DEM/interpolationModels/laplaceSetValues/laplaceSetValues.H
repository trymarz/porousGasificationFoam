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

#ifndef laplaceSetValues_H
#define laplaceSetValues_H

#include "InterpolationModel.H"

namespace Foam
{

// Hard set-values Laplacian interpolation: cells holding particles and cells
// outside the solid region are hard-pinned via setValues(); the solve fills
// the gaps between them. Templated over Type to serve both Us (vector) and
// lambda (scalar) -- see InterpolationModel.H.
template<class Type>
class LaplaceSetValuesInterpolation
:
    public InterpolationModel<Type>
{
private:
    label nSetValuesCorrectors_;

public:
    LaplaceSetValuesInterpolation
    (
        const dictionary& dict,
        const fvMesh& mesh,
        GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
        GeometricField<Type, fvPatchField, volMesh>& field,
        const volScalarField& nParticles,
        const volScalarField& porosityF,
        const Type& backgroundValue
    );

    virtual ~LaplaceSetValuesInterpolation() = default;

    virtual void interpolate() override;
};

} // namespace Foam

#endif
