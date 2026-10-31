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

#include "multiComponentSolidMixture.H"
#include "fixedValueFvPatchFields.H"

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

template<class ThermoSolidType>
void Foam::multiComponentSolidMixture<ThermoSolidType>::correctMassFractions()
{
    volScalarField Yt("Yt", Y_[0]);

    for (label n=1; n<Y_.size(); n++) 
    {
        Yt += Y_[n];
    }

    forAll(Yt,cellI)
    {
    	if (Yt[cellI] == 0)
        {
            Yt[cellI] = 1;
        }
    }

    forAll(Y_, n)
    {
        Y_[n].primitiveFieldRef() /= Yt.internalField();
    }
}


template<class ThermoSolidType>
bool Foam::multiComponentSolidMixture<ThermoSolidType>::prescribesComposition
(
    label patchi
) const
{
    forAll(Y_, i)
    {
        if (!isA<fixedValueFvPatchScalarField>(Y_[i].boundaryField()[patchi]))
        {
            return false;
        }
    }
    return true;
}


template<class ThermoSolidType>
template<class Property>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::patchProperty
(
    const scalarField& Tp,
    label patchi,
    const Property& property
) const
{
    const bool fixed = prescribesComposition(patchi);
    const labelUList& cells = Y_[0].mesh().boundary()[patchi].faceCells();

    tmp<scalarField> tf(new scalarField(Tp.size()));
    scalarField& f = tf.ref();

    forAll(Tp, facei)
    {
        scalar sum = 0.0;
        if (fixed)
        {
            forAll(Y_, i)
            {
                sum += Y_[i].boundaryField()[patchi][facei];
            }
        }

        // A patch that prescribes no composition (sum 0) reads the face cell's
        f[facei] =
            sum > SMALL
          ? property(Tp[facei], patchFaceY(Y_, patchi, facei, sum))
          : property(Tp[facei], cellY(Y_, cells[facei]));
    }

    return tf;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::X
(
    label iComp, const Composition& Y, scalar T
) const
{
    scalar rhoInv = 0.0;

    forAll(solidData_, i)
    {
        rhoInv += Y[i] / solidData_[i].rho(T);
    }
    if (rhoInv == 0)
    {
       rhoInv = 1;
    }
    scalar X = Y[iComp] / solidData_[iComp].rho(T);
    return (X / rhoInv);
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class ThermoSolidType>
Foam::multiComponentSolidMixture<ThermoSolidType>::multiComponentSolidMixture
(
    const dictionary& thermoSolidDict,
    const fvMesh& mesh
)
:
    basicSolidMixture
    (
        thermoSolidDict.lookup("solidComponents"),
        mesh
    ),
    solidData_(components_.size())
{

    forAll(components_, i)
    {
        solidData_.set
        (
            i,
            new ThermoSolidType
            (
                thermoSolidDict.subDict(components_[i] + "Coeffs")
            )
        );
    }
    correctMassFractions();

}



// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::rhoOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].rho(T) * X(i, Y, T);
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hfOf
(
    const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].hf() * Y[i];
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hsOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].hs(T)*Y[i];
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].h(T)*Y[i];
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::kappaOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].kappa(T)*X(i, Y, T);
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::sigmaSOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].sigmaS(T)*X(i, Y, T);
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::KOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].K(T) * X(i, Y, T);
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::emissivityOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].emissivity(T) * X(i, Y, T);
    }
    return tmp;
}


template<class ThermoSolidType>
template<class Composition>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::CpOf
(
    scalar T, const Composition& Y
) const
{
    scalar tmp = 0.0;
    forAll(solidData_, i)
    {
        tmp += solidData_[i].Cp(T) * Y[i];
    }
    return tmp;
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::rho
(
    scalar T, label celli
) const
{
    return rhoOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hf
(
    scalar, label celli
) const
{
    return hfOf(cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hs
(
    scalar T, label celli
) const
{
    return hsOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::h
(
    scalar T, label celli
) const
{
    return hOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::kappa
(
    scalar T, label celli
) const
{
    return kappaOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::sigmaS
(
    scalar T, label celli
) const
{
    return sigmaSOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::K
(
    scalar T, label celli
) const
{
    return KOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::emissivity
(
    scalar T, label celli
) const
{
    return emissivityOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::Cp
(
    scalar T, label celli
) const
{
    return CpOf(T, cellY(Y_, celli));
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::rho
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return rhoOf(T, Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::kappa
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return kappaOf(T, Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::sigmaS
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return sigmaSOf(T, Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::K
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return KOf(T, Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::emissivity
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return emissivityOf(T, Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::hf
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar, const auto& Y) { return hfOf(Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::hs
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return hsOf(T, Y); }
    );
}


template<class ThermoSolidType>
Foam::tmp<Foam::scalarField>
Foam::multiComponentSolidMixture<ThermoSolidType>::Cp
(
    const scalarField& Tp, label patchi
) const
{
    return patchProperty
    (
        Tp,
        patchi,
        [this](scalar T, const auto& Y) { return CpOf(T, Y); }
    );
}


template<class ThermoSolidType>
void Foam::multiComponentSolidMixture<ThermoSolidType>::read
(
    const dictionary& thermoDict
)
{
    forAll(components_, i)
    {
        solidData_[i] =
            ThermoSolidType(thermoDict.subDict(components_[i] + "Coeffs"));
    }
}


// ************************************************************************* //
