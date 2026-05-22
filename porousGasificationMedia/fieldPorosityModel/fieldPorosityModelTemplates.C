/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2012-2018 OpenFOAM Foundation
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

Description
    Per-cell numerical core of the porous resistance: builds the
    Darcy+Forchheimer drag tensor and splits it into an isotropic
    diagonal contribution and a non-isotropic source contribution
    going into UEqn. Templated on the rho type so that incompressible
    and compressible call sites can both use it.

\*---------------------------------------------------------------------------*/

#include "fieldPorosityModel.H"
#include "fvm.H"

// * * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * //

//- Build the Darcy+Forchheimer drag tensor per cell and post it into
//  the UEqn diagonal/source.
//
//  Drag tensor per cell:
//      D_drag = (mu + f * rho * |U| * sqrt(3) / |Df|) * Df
//  (with the Forchheimer term gated on |Df| != 0 to avoid div/0).
//
//  Split into an isotropic part (trace) which goes implicitly into
//  the matrix diagonal and a deviatoric part which goes explicitly
//  into the source — the standard treatment for anisotropic
//  resistances so the linear solve stays diagonally dominant.
template<class RhoFieldType>
void Foam::fieldPorosityModel::addViscousInertialResistance
(
    scalarField& Udiag,
    vectorField& Usource,
    const labelList& cells,
    const scalarField& V,
    const RhoFieldType& rho,
    const scalarField& mu,
    const vectorField& U,
    tensorField& Df
) const
{
    forAll (cells, i)
    {
        tensor dragCoeff;
        if (mag(Df[cells[i]]) != 0)
        {
            // Darcy + Forchheimer.
            dragCoeff = (
                            mu[cells[i]] + f_*rho[cells[i]]*mag(U[cells[i]])*sqrt(3.)/mag(Df[cells[i]])
                        )*Df[cells[i]];
        }
        else
        {
            // Pure Darcy (mu * Df) when |Df| == 0.
            dragCoeff = mu[cells[i]]*Df[cells[i]];
        }

        // Isotropic part -> implicit (diagonal).
        scalar isoDragCoeff = tr(dragCoeff);

        Udiag[cells[i]] += V[cells[i]]*isoDragCoeff;

        // Deviatoric part -> explicit (source). I*tr(d) is the
        // isotropic projection that was already absorbed into the
        // diagonal above.
        Usource[cells[i]] -=
            V[cells[i]] * ((dragCoeff - I * isoDragCoeff) & U[cells[i]]);
    }
}

// ************************************************************************* //
