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

#include "HGSSolidMixtureThermo.H"
#include "fvMesh.H"


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

template<class MixtureType>
void Foam::HGSSolidMixtureThermo<MixtureType>::calculate()
{

    scalarField& rhoCells = this->rho_.primitiveFieldRef();
    scalarField& KCells = this->K_.primitiveFieldRef();
    scalarField& kappaCells = this->kappa_.primitiveFieldRef();
    scalarField& sigmaSCells = this->sigmaS_.primitiveFieldRef();
    scalarField& emissivityCells = this->emissivity_.primitiveFieldRef();

    forAll(T_.internalField(), celli)
    {
        rhoCells[celli] = MixtureType::rho(T_[celli], celli);
        kappaCells[celli] = MixtureType::kappa(T_[celli], celli);
        sigmaSCells[celli] = MixtureType::sigmaS(T_[celli], celli);
        KCells[celli] = MixtureType::K(T_[celli], celli);
        emissivityCells[celli] = MixtureType::emissivity(T_[celli], celli);
    }
    forAll(T_.boundaryField(), patchI)
    {
        rho_.boundaryFieldRef()[patchI] == this->rho(patchI)();
        K_.boundaryFieldRef()[patchI] == this->K(patchI)();
        kappa_.boundaryFieldRef()[patchI] == this->kappa(patchI)();
        sigmaS_.boundaryFieldRef()[patchI] == this->sigmaS(patchI)();
        emissivity_.boundaryFieldRef()[patchI] == this->emissivity(patchI)();
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class MixtureType>
Foam::HGSSolidMixtureThermo<MixtureType>::HGSSolidMixtureThermo
(
    const fvMesh& mesh
)
:
    HGSSolidThermo(mesh),
    MixtureType(*this, mesh),
    K_
    (
        IOobject
        (
            "K",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimEnergy/dimTime/(dimLength*dimTemperature)
    )
{
    calculate();
}


template<class MixtureType>
Foam::HGSSolidMixtureThermo<MixtureType>::HGSSolidMixtureThermo
(
    const fvMesh& mesh,
    const dictionary& dict
)
:
    HGSSolidThermo(mesh, dict),
    MixtureType(*this, mesh),
    K_
    (
        IOobject
        (
            "K",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimEnergy/dimTime/(dimLength*dimTemperature)
    )
{
    calculate();
}


template<class MixtureType>
Foam::HGSSolidMixtureThermo<MixtureType>::HGSSolidMixtureThermo
(
    const fvMesh& mesh,
    const dictionary& dict,
    const PtrList<volScalarField>& gasPhaseGases
)
:
    HGSSolidThermo(mesh, dict),
    MixtureType(*this, mesh, gasPhaseGases),
    K_
    (
        IOobject
        (
            "K",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimEnergy/dimTime/(dimLength*dimTemperature)
    )
{
    calculate();
}
// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

template<class MixtureType>
Foam::HGSSolidMixtureThermo<MixtureType>::~HGSSolidMixtureThermo()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class MixtureType>
void Foam::HGSSolidMixtureThermo<MixtureType>::correct()
{
    calculate();
}


template<class MixtureType>
const Foam::volScalarField& Foam::HGSSolidMixtureThermo<MixtureType>::K() const
{
    return K_;
}


template<class MixtureType>
Foam::tmp<Foam::volScalarField>
Foam::HGSSolidMixtureThermo<MixtureType>::Cp() const
{
    tmp<volScalarField> tCp
    (
        new volScalarField
        (
            IOobject
            (
                "Cp",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimEnergy/(dimMass*dimTemperature)
        )
    );
    volScalarField& Cp = tCp.ref();

    forAll(T_.internalField(), celli)
    {
        Cp[celli] = MixtureType::Cp(T_[celli], celli);
    }

    forAll(Cp.boundaryField(), patchI)
    {
        Cp.boundaryFieldRef()[patchI] == this->Cp(patchI)();
    }

    return tCp;
}


template<class MixtureType>
Foam::tmp<Foam::volScalarField>
Foam::HGSSolidMixtureThermo<MixtureType>::hs() const
{
    tmp<volScalarField> ths
    (
        new volScalarField
        (
            IOobject
            (
                "Hs",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimEnergy/(dimMass*dimTemperature)
        )
    );
    volScalarField& hs = ths.ref();

    forAll(T_.internalField(), celli)
    {
        hs[celli] = MixtureType::hs(T_[celli], celli);
    }

    forAll(hs.boundaryField(), patchI)
    {
        hs.boundaryFieldRef()[patchI] == this->hs(patchI)();
    }

    return ths;
}


template<class MixtureType>
Foam::tmp<Foam::volScalarField>
Foam::HGSSolidMixtureThermo<MixtureType>::Hf() const
{
    tmp<volScalarField> thF
    (
        new volScalarField
        (
            IOobject
            (
                "hF",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimEnergy/(dimMass*dimTemperature)
        )
    );
    volScalarField& hf = thF.ref();

    forAll(T_.internalField(), celli)
    {
        hf[celli] = MixtureType::hf(T_[celli], celli);
    }

    forAll(hf.boundaryField(), patchI)
    {
        hf.boundaryFieldRef()[patchI] == this->Hf(patchI)();
    }

    return thF;
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::rho
(
    const label patchI
) const
{
    return MixtureType::rho(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::Cp
(
    const label patchI
) const
{
    return MixtureType::Cp(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::hs
(
    const label patchI
) const
{
    return MixtureType::hs(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::K
(
    const label patchI
) const
{
    return MixtureType::K(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::Hf
(
    const label patchI
) const
{
    return MixtureType::hf(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::sigmaS
(
    const label patchI
) const
{
    return MixtureType::sigmaS(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::kappa
(
    const label patchI
) const
{
    return MixtureType::kappa(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
Foam::tmp<Foam::scalarField> Foam::HGSSolidMixtureThermo<MixtureType>::emissivity
(
    const label patchI
) const
{
    return MixtureType::emissivity(T_.boundaryField()[patchI], patchI);
}


template<class MixtureType>
bool Foam::HGSSolidMixtureThermo<MixtureType>::read()
{
    if (HGSSolidThermo::read())
    {
        MixtureType::read(*this);
        return true;
    }
    else
    {
        return false;
    }
}


template<class MixtureType>
bool Foam::HGSSolidMixtureThermo<MixtureType>::writeData(Ostream& os) const
{
     bool ok = HGSSolidThermo::writeData(os);
     return ok && os.good();
}


// ************************************************************************* //
