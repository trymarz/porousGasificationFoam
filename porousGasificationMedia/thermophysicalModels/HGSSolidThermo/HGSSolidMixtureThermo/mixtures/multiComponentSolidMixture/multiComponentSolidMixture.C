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
typename Foam::multiComponentSolidMixture<ThermoSolidType>::massFractions
Foam::multiComponentSolidMixture<ThermoSolidType>::faceY
(
    label patchi, label facei
) const
{
    scalar sum = 0.0;

    forAll(Y_, i)
    {
        const fvPatchScalarField& Yp = Y_[i].boundaryField()[patchi];
        if (!Yp.fixesValue())
        {
            sum = 0.0;
            break;
        }
        sum += Yp[facei];
    }

    // A patch that prescribes no composition (sum 0) reads the face cell's
    if (sum > SMALL)
    {
        return massFractions(Y_, patchi, facei, sum);
    }

    return massFractions
    (
        Y_,
        Y_[0].mesh().boundary()[patchi].faceCells()[facei]
    );
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::X
(
    label iComp, const massFractions& Y, scalar T
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::rho
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hf
(
    const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hs
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::h
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::kappa
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::sigmaS
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::K
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::emissivity
(
    scalar T, const massFractions& Y
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
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::Cp
(
    scalar T, const massFractions& Y
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
    return rho(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hf
(
    scalar, label celli
) const
{
    return hf(massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hs
(
    scalar T, label celli
) const
{
    return hs(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::h
(
    scalar T, label celli
) const
{
    return h(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::kappa
(
    scalar T, label celli
) const
{
    return kappa(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::sigmaS
(
    scalar T, label celli
) const
{
    return sigmaS(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::K
(
    scalar T, label celli
) const
{
    return K(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::emissivity
(
    scalar T, label celli
) const
{
    return emissivity(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::Cp
(
    scalar T, label celli
) const
{
    return Cp(T, massFractions(Y_, celli));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::rho
(
    scalar T, label patchi, label facei
) const
{
    return rho(T, faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hf
(
    scalar T, label patchi, label facei
) const
{
    return hf(faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::hs
(
    scalar T, label patchi, label facei
) const
{
    return hs(T, faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::kappa
(
    scalar T, label patchi, label facei
) const
{
    return kappa(T, faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::sigmaS
(
    scalar T, label patchi, label facei
) const
{
    return sigmaS(T, faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::K
(
    scalar T, label patchi, label facei
) const
{
    return K(T, faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::emissivity
(
    scalar T, label patchi, label facei
) const
{
    return emissivity(T, faceY(patchi, facei));
}


template<class ThermoSolidType>
Foam::scalar Foam::multiComponentSolidMixture<ThermoSolidType>::Cp
(
    scalar T, label patchi, label facei
) const
{
    return Cp(T, faceY(patchi, facei));
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
