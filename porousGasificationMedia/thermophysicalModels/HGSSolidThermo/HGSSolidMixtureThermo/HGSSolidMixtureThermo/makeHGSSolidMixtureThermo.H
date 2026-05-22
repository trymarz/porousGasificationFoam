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

InClass
    Foam::HGSSolidMixtureThermo

Description
    Per-instantiation macro:

      makeHGSSolidMixtureThermo(Mixture, ThermoPhysics)

    Typedefs \c HGSSolidMixtureThermo<Mixture<ThermoPhysics>>, registers
    its run-time name (as the bracket-and-comma composition that
    user-facing solidThermoType strings expect) and adds it to each of
    the three RTS tables declared in HGSSolidThermo. Expanded from
    HGSSolidMixtureThermos.C against the permitted mixture/thermo
    combinations.

\*---------------------------------------------------------------------------*/

#ifndef makeSolidMixtureThermo_H
#define makeSolidMixtureThermo_H

#include "addToRunTimeSelectionTable.H"
#include "HGSSolidMixtureThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#define makeSolidMixtureThermo(CThermo,MixtureThermo,Mixture,Transport,Radiation,Thermo,Rho)\
                                                                              \
typedef MixtureThermo<Mixture<Transport<Radiation<Thermo<Rho> > > > >         \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho;                \
                                                                              \
defineTemplateTypeNameAndDebugWithName                                        \
(                                                                             \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho,                \
    #MixtureThermo                                                            \
    "<"#Mixture"<"#Transport"<"#Radiation"<"#Thermo"<"#Rho">>>>>",            \
    0                                                                         \
);                                                                            \
                                                                              \
addToRunTimeSelectionTable                                                    \
(                                                                             \
    CThermo,                                                                  \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho,                \
    mesh                                                                      \
);                                                                            \
                                                                              \



#define makeSolidHeterogeneousMixtureThermo(CThermo,MixtureThermo,Mixture,Transport,Radiation,Thermo,Rho)\
                                                                              \
typedef MixtureThermo<Mixture<Transport<Radiation<Thermo<Rho> > > > >         \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho;                \
                                                                              \
defineTemplateTypeNameAndDebugWithName                                        \
(                                                                             \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho,                \
    #MixtureThermo                                                            \
    "<"#Mixture"<"#Transport"<"#Radiation"<"#Thermo"<"#Rho">>>>>",            \
    0                                                                         \
);                                                                            \
                                                                              \
addToRunTimeSelectionTable                                                    \
(                                                                             \
    CThermo,                                                                  \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho,                \
    gasPhase                                                                  \
);                                                                            \
                                                                              \



#define makeSolidHeterogeneousMixtureThermoName(CThermo,MixtureThermo,Mixture,Transport,Radiation,Thermo,Rho,name)\
                                                                              \
typedef MixtureThermo<Mixture<Transport<Radiation<Thermo<Rho> > > > >         \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho##name;          \
                                                                              \
defineTemplateTypeNameAndDebugWithName                                        \
(                                                                             \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho##name,          \
    "solidMixtureThermo"                                                      \
    "<"#name">",                                                             \
    0                                                                         \
);                                                                            \
                                                                              \
addToRunTimeSelectionTable                                                    \
(                                                                             \
    CThermo,                                                                  \
    MixtureThermo##Mixture##Transport##Radiation##Thermo##Rho##name,          \
    gasPhase                                                                  \
);                                                                            \
                                                                              \



// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
