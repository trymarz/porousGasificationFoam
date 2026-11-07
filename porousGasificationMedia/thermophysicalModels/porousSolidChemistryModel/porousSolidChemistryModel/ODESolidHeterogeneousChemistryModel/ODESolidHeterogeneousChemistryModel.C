/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2020 OpenFOAM Foundation
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

#include "ODESolidHeterogeneousChemistryModel.H"
#include "multiComponentMixture.H"
#include "UniformField.H"
#include "extrapolatedCalculatedFvPatchFields.H"
#include "reactingSolidHeterogeneousMixture.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::ODESolidHeterogeneousChemistryModel
(
    const SolidThermo& solidThermo,
    PtrList<volScalarField>& gasPhaseGases
)
:
    porousThermoSolidChemistryModel<SolidThermo>(solidThermo),
    ODESystem(),
    mesh_(solidThermo.mesh()),
    gasPhaseGases_(gasPhaseGases),
    Ys_(this->solidThermo().composition().Y()),
    solidThermo_
    (
        static_cast<const reactingSolidHeterogeneousMixture<SolidThermoType>&>
            (this->solidThermo().composition()).solidData()
    ),
    gasThermo_(
        dynamic_cast<const multiComponentMixture<GasThermoType>&>
        (mesh_.lookupObject <psiReactionThermo> ("thermophysicalProperties")).speciesData()
    ),
    pyrolisisGases_
    (
        mesh_.lookupObject<dictionary>
            ("chemistryProperties").lookup("species")
    ),
    reactions_
    (
        static_cast<const reactingSolidHeterogeneousMixture<SolidThermoType>&>
            (this->solidThermo().composition())
    ),
    nGases_(pyrolisisGases_.size()),
    nSpecie_(Ys_.size()+ nGases_),
    nSolids_(Ys_.size()),
    nReaction_(reactions_.size()),
    Treact_
    (
        porousThermoSolidChemistryModel<SolidThermo>::template lookupOrDefault<scalar>
        (
            "Treact",
            0
        )
    ),
    RRs_(nSolids_),
    RRg_(nGases_),
    RR_(nSpecie_),
    c_(nSpecie_),
    dcdt_(nSpecie_),
    shReactionHeat_
    (
        IOobject
        (
            "shReactionHeat",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
            mesh_,
            dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    coeffs_(nSpecie_ + 2),
    Ys0_(nSolids_),
    cellCounter_(0),
    reactingCells_(mesh_.nCells(), true),
    V_
    (
        IOobject
        (
            "V_",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
    dimensionedScalar("zero",dimVolume,0.0)
    ),
    solidReactionEnergyFromEnthalpy_
    (
        mesh_.lookupObject<dictionary>
            ("chemistryProperties").lookupOrDefault("solidReactionEnergyFromEnthalpy",true)
    ),
    stoichiometricReactions_
    (
        mesh_.lookupObject<dictionary>
            ("chemistryProperties").lookupOrDefault("stoichiometricReactions",false)
    ),
    diffusionLimitedReactions_
    (
        mesh_.lookupObject<dictionary>
            ("chemistryProperties").lookupOrDefault("diffusionLimitedReactions",false)
    ),
    solidReactionDeltaEnergy_(0.0),
    showRRR_
    (
        mesh_.lookupObject<dictionary>
            ("chemistryProperties").lookupOrDefault("showRelativeReactionRates",false)
    ),
    rhoG_
    (
        mesh_.lookupObject<volScalarField>("rho")
    ),
    rho_
    (
        solidThermo.rho()
    ),
    porosityF_
    (
      mesh_.lookupObject<volScalarField>("porosityF")
    ),
    ST_
    (
        mesh_.lookupObject<volScalarField>("STvol")
    ),
    specieConcentration_(nSpecie_, 0.0),
    tauC_(0.0),
    dt_(0.0),
    subStepHeat_(0.0),
    heatOffset_(0.0)
{
    logGases();
    createSourceFields();
    logReactions();
    warnSolidSubstrateCoefficients();
    buildGasDictionaries();
    buildStoichiometry();
    buildRates();

    Info << "diffusionLimitedReactions " << diffusionLimitedReactions_ << nl;
}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
logGases() const
{
    Info << "gases in gas phase: " << gasPhaseGases_.size() << " \n" << endl;

    forAll(gasPhaseGases_,i)
    {
        Info << gasPhaseGases_[i].name() << " \n";
    }

    Info << "gases from pyrolysis: " << endl;

    forAll(pyrolisisGases_,i)
    {
        Info << pyrolisisGases_[i] << " \n";
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
createSourceFields()
{
    forAll(RRs_, fieldI)
    {
        RRs_.set
        (
            fieldI,
            new scalarField(mesh_.nCells(), 0.0)
        );


        IOobject header
        (
            Ys_[fieldI].name() + "0",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ
        );

        // check if field exists and can be read
        if (header.typeHeaderOk<volScalarField>(true))
        {
            Ys0_.set
            (
                fieldI,
                new volScalarField
                (
                    IOobject
                    (
                        Ys_[fieldI].name() + "0",
                        mesh_.time().timeName(),
                        mesh_,
                        IOobject::MUST_READ,
                        IOobject::AUTO_WRITE
                    ),
                    mesh_
                )
            );
        }
        else
        {
            volScalarField Y0Default
            (
                IOobject
                (
                    "Y0Default",
                    mesh_.time().timeName(),
                    mesh_,
                    IOobject::MUST_READ,
                    IOobject::NO_WRITE
                ),
                mesh_
            );

            Ys0_.set
            (
                fieldI,
                new volScalarField
                (
                    IOobject
                    (
                        Ys_[fieldI].name() + "0",
                        mesh_.time().timeName(),
                        mesh_,
                        IOobject::NO_READ,
                        IOobject::AUTO_WRITE
                    ),
                    Y0Default
                )
            );

            // Calculate inital values of Ysi0 = rho*delta*Yi
            Ys0_[fieldI] = this->solidThermo().rho() * Ys_[fieldI] * dimensionedScalar("tmp",dimVol/dimMass,1.);
            Ys0_[fieldI].ref() *= mesh_.V();
        }
    }

    V_.ref() = mesh_.V();

    forAll(RRg_, fieldI)
    {
        RRg_.set(fieldI, new scalarField(mesh_.nCells(), 0.0));
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
logReactions() const
{
    Info<< "ODESolidHeterogeneousChemistryModel: Number of solids = " << nSolids_
        << " and reactions = " << nReaction_ << endl;

    Info<< "Number of gases from pyrolysis = " << nGases_ << endl;

    forAll(reactions_, i)
    {
        Info<< indent << "Reaction " << i << nl << reactions_[i] << nl;
    }

    Info<< "solidReactionEnergyFromEnthalpy " << solidReactionEnergyFromEnthalpy_ << nl;
    Info<< "stoichiometricReactions " << stoichiometricReactions_ << nl;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
warnSolidSubstrateCoefficients() const
{
    // Rates are per kg of solid substrate (CPC 2022, Eq. 4-5), so a lone
    // solid LHS reactant's coefficient cancels. Branch fractions belong in A.
    forAll(reactions_, i)
    {
        const solidHeterogeneousReaction& R = reactions_[i];
        if
        (
            R.slhs().size() == 1 && R.glhs().size() == 0
         && mag(R.slhsSto()[0] - 1.0) > SMALL
        )
        {
            WarningIn("ODESolidHeterogeneousChemistryModel")
                << "Reaction " << i << " has a single solid substrate with"
                << " coefficient " << R.slhsSto()[0] << ". The rate is per kg"
                << " of solid substrate, so this coefficient has no effect on"
                << " the rate; scale A instead.\n" << R << endl;
        }
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
buildGasDictionaries()
{
    gasDictionary_.resize(nGases_);
    forAll(gasDictionary_,gasI)
    {
        forAll(gasPhaseGases_,gasJ)
        {
            if (gasPhaseGases_[gasJ].name() == pyrolisisGases_[gasI])
            {
                gasDictionary_[gasI] = gasJ;
            }
        }
    }

    gasDictionaryBack_.resize(gasPhaseGases_.size());
    forAll(gasDictionaryBack_,gasJ)
    {
        gasDictionaryBack_[gasJ] = -1;
        forAll(pyrolisisGases_,gasI)
        {
            if (gasPhaseGases_[gasJ].name() == pyrolisisGases_[gasI])
            {
                gasDictionaryBack_[gasJ] = gasI;
            }
        }
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
buildStoichiometry()
{
    // Molar masses in gas-phase order, as gasDictionary_ and glhs() index them.
    scalarList W(gasThermo_.size());
    forAll(W, j)
    {
        W[j] = gasThermo_[j].W();
    }

    stoichiometry_.setSize(nReaction_);
    forAll(reactions_, i)
    {
        stoichiometry_.set
        (
            i,
            new solidReactionStoichiometry
            (
                reactions_[i],
                stoichiometricReactions_,
                W,
                gasDictionary_,
                gasDictionaryBack_,
                nSolids_
            )
        );
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
buildRates()
{
    const solidReactionRate::cellFields rateFields
    {
        this->solidThermo().rho(), rhoG_, ST_, porosityF_
    };

    rates_.setSize(nReaction_);
    forAll(reactions_, i)
    {
        rates_.set
        (
            i,
            new solidReactionRate
            (
                reactions_[i],
                stoichiometry_[i],
                gasDictionaryBack_,
                nSolids_,
                diffusionLimitedReactions_,
                rateFields
            )
        );
    }
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
~ODESolidHeterogeneousChemistryModel()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //
template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::
setCellReacting(const label cellI, const bool active)
{
    if (reactingCells_[cellI] != active && active)
    {
        for (label i=0; i<nSolids_; i++)
        {
            specieConcentration_[i] = this->solidThermo().rho()[cellI]*Ys_[i][cellI];
        }
    }
    reactingCells_[cellI] = active;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::scalarField  Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::omega
(
    const scalarField& c,
    const scalar T,
    const scalar p,
    scalarField& rR,
    const bool updateC0
) const
{
    const label cellI = cellCounter_;
    scalarField om(nEqns(),0.0);

    scalarField c1(nSpecie_, 0.0);
    for (label i=0; i<nSpecie_; i++)
    {
        c1[i] = max(0.0, c[i]);
    }

    forAll(reactions_, i)
    {
        const solidHeterogeneousReaction& R = reactions_[i];

        const solidReactionStoichiometry& st = stoichiometry_[i];

        const scalar omegai = rates_[i].rate(cellI, T, c1);

        forAll(st.species(), k)
        {
            om[st.species()[k]] += omegai*st.coeffs()[k];
        }

        if (not solidReactionEnergyFromEnthalpy_)
        {
            om[heatEqn()] -= omegai*st.heat();
        }

        if (updateC0)
        {
            forAll(R.slhs(), s)
            {
                label si = R.slhs()[s];
                Ys0_[si][cellI] = this->solidThermo().rho()[cellI] *Ys_[si][cellI] * V_[cellI];
            }
            forAll(R.srhs(), s)
            {
                label si = R.srhs()[s];
                Ys0_[si][cellI] = this->solidThermo().rho()[cellI] *Ys_[si][cellI] * V_[cellI];
            }
        }

        rR[i] = mag(omegai*st.consumed());
    }
    return om;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::derivatives
(
    const scalar time,
    const scalarField& c,
    scalarField& dcdt
) const
{
    scalar T = c[nSpecie_];

    dcdt = 0.0;
    scalarField rR(nReaction_,0.0);

    label celli = cellCounter_;

    scalarField omegaPreq(omega(c,T,0,rR) * (1. - porosityF_[celli]));
    forAll(dcdt,i)
    {
        dcdt[i] = omegaPreq[i];
    }

    //Total mass concentration
    scalar cTot = 0.0;
    for (label i=0; i<nSolids_; i++)
    {
        cTot += c[i];
    }

    scalar newCp = 0.0;
    scalar newhi = 0.0;

    if (solidReactionEnergyFromEnthalpy_)
    {
        for (label i=0; i<nSolids_; i++)
        {
                scalar dYidt = dcdt[i];
                scalar Yi = c[i];
                newCp += Yi*solidThermo_[i].Cp(T);
                newhi -= dYidt*solidThermo_[i].hf();
        }
    }
    else
    {
        for (label i=0; i<nSolids_; i++)
        {
                scalar Yi = c[i];
                newCp += Yi*solidThermo_[i].Cp(T);
        }
        newhi += omegaPreq[heatEqn()];
    }

    // Reaction heating rate of the solid, guarded against a vanishing heat
    // capacity. newCp is built from the solid concentrations c[i], so it tends
    // to zero as the solid is consumed; in the branch above that takes newhi
    // from omegaPreq[heatEqn()], newhi does not vanish with it, leaving
    // newhi/newCp to overflow. The limiter below cannot absorb that: an
    // infinite dTdt makes the scaling inf/inf, i.e. NaN. Guard the division
    // instead -- with no solid heat capacity there is no heating rate.
    scalar dTdt = 0;
    if (newhi != 0 && newCp > VSMALL)
    {
        dTdt = newhi/newCp;
    }

    // Bound the reaction heating to |dT/dt| <= 500 K/s, preserving its sign.
    const scalar dtMag = min(500.0, mag(dTdt));
    dcdt[nSpecie_] = dTdt*dtMag/(mag(dTdt) + 1.0e-10);

    // dp/dt = ...
    dcdt[nSpecie_ + 1] = 0.0;
}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::jacobian
(
    const scalar t,
    const scalarField& c,
    scalarField& dcdt,
    scalarSquareMatrix& dfdc
) const
{

    label cellI = cellCounter_;

    scalar T = c[nSpecie_];

    scalarField c2(nSpecie_, 0.0);
    scalarField rR(nReaction_, 0.0);

    for (label i=0; i<nSpecie_; i++)
    {
        c2[i] = max(c[i], 0.0);
    }

    for (label i=0; i<nEqns(); i++)
    {
        for (label j=0; j<nEqns(); j++)
        {
            dfdc[i][j] = 0.0;
        }
    }

    // df/dt: the right-hand side does not depend on time (Rosenbrock reads this).
    dcdt = 0.0;

    // Stoichiometry x d((1 - porosity) r): see solidReactionRate for what
    // the derivative is taken in (#200).
    forAll(rates_, ri)
    {
        const solidReactionRate& rate = rates_[ri];
        const solidReactionStoichiometry& st = stoichiometry_[ri];

        scalarField dr(rate.reactants().size());
        rate.rateDerivative(cellI, T, c2, dr);

        forAll(rate.reactants(), j)
        {
            const label sj = rate.reactants()[j];

            forAll(st.species(), k)
            {
                dfdc[st.species()[k]][sj] += dr[j]*st.coeffs()[k];
            }

            if (not solidReactionEnergyFromEnthalpy_)
            {
                dfdc[heatEqn()][sj] -= dr[j]*st.heat();
            }
        }
    }

    // calculate the dcdT elements numerically
    scalar delta = 1.0e-8;

    scalarField dcdT0(dcdt);
    scalarField omegaPreq(omega(c2,T - delta ,0,rR)*(1.-porosityF_[cellI]));

    forAll(dcdT0,i)
    {
        dcdT0[i] = omegaPreq[i];
    }

    scalarField dcdT1(dcdt);
    omegaPreq = omega(c2,T + delta,0,rR)*(1.-porosityF_[cellI]);
    forAll(dcdT1,i)
    {
        dcdT1[i] = omegaPreq[i];
    }

    for (label i=0; i<nEqns(); i++)
    {
        dfdc[i][nSpecie_] = 0.5*(dcdT1[i] - dcdT0[i]) / delta;
    }

}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::startHeatIntegration
(
    scalarField& cTp
) const
{
    if (solidReactionEnergyFromEnthalpy_)
    {
        return;
    }

    // Start from the solid's sensible heat, not 0: the ODE solver's tolerance
    // is relative, so a state starting at 0 would be resolved to absTol.
    const scalar T = cTp[nSpecie_];

    heatOffset_ = 0.0;
    for (label i=0; i<nSolids_; i++)
    {
        heatOffset_ += cTp[i]*solidThermo_[i].Cp(T)*T;
    }

    cTp[heatEqn()] = heatOffset_;
}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::finishHeatIntegration
(
    const scalarField& cTp
) const
{
    if (not solidReactionEnergyFromEnthalpy_)
    {
        subStepHeat_ = cTp[heatEqn()] - heatOffset_;
    }
}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::tmp<Foam::volScalarField>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::tc() const
{
    tmp<volScalarField> ttc
    (
        volScalarField::New
        (
            "tc",
            this->mesh(),
            dimensionedScalar(dimTime, SMALL),
            extrapolatedCalculatedFvPatchScalarField::typeName
        )
    );


    return ttc;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::tmp<Foam::volScalarField>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::gasHs
(
    const volScalarField& p,
    const volScalarField& T,
    const label index
) const
{

    tmp<volScalarField> tHs
    (
        new volScalarField
        (
            IOobject
            (
                "Hs_" + pyrolisisGases_[index],
                this->mesh_.time().timeName(),
                this->mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            this->mesh_,
            dimensionedScalar("zero", dimEnergy/dimMass, 0.0),
            zeroGradientFvPatchScalarField::typeName
        )
    );

    volScalarField& gasHs = tHs.ref();

    const GasThermoType& mixture = gasThermo_[index];

    forAll(gasHs.internalField(), cellI)
    {
        gasHs[cellI] = mixture.Hs(p[cellI], T[cellI]);
    }

    return tHs;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::tmp<Foam::volScalarField>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::Sh() const
{
    tmp<volScalarField> tSh
    (
        new volScalarField
        (
            IOobject
            (
                "Sh",
                this->mesh_.time().timeName(),
                this->mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            this->mesh_,
            dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0),
            zeroGradientFvPatchScalarField::typeName
        )
    );

    if (this->chemistry_)
    {
        scalarField& Sh = tSh.ref();

        if (solidReactionEnergyFromEnthalpy_)
        {
            forAll(Ys_, i)
            {
            forAll(Sh, cellI)
            {
                scalar hf = solidThermo_[i].hf();
                Sh[cellI] -= hf*RRs_[i][cellI];
            }
            }
            forAll(Sh, cellI)
            {
            forAll(pyrolisisGases_, i)
            {
                scalar Hc = gasThermo_[gasDictionary_[i]].Hc();
                Sh[cellI] -= Hc*RRg_[i][cellI];
            }
            }
        }
        else
        {
            forAll(Sh,cellI)
            {
            Sh[cellI] = shReactionHeat_[cellI];
            }
        }
    }
    return tSh;
}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::tmp<Foam::volScalarField>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::Qdot() const
{
    tmp<volScalarField> tQdot
    (
        volScalarField::New
        (
            "Qdot",
            this->mesh_,
            dimensionedScalar(dimEnergy/dimVolume/dimTime, 0)
        )
    );

    if (this->chemistry_)
    {
        scalarField& Qdot = tQdot.ref();

        forAll(Ys_, i)
        {
            forAll(Qdot, celli)
            {
                const scalar hi = 0;
                Qdot[celli] -= hi*RR_[i][celli];
            }
        }
    }

    return tQdot;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::tmp<Foam::volScalarField>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::RRpor(const volScalarField T) const
{
    tmp<volScalarField> tRRpor
    (
        new volScalarField
        (
            IOobject
            (
                "RRpor",
                this->mesh_.time().timeName(),
                this->mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            this->mesh_,
            dimensionedScalar("zero", dimless/dimTime, 0.0),
            zeroGradientFvPatchScalarField::typeName
        )
    );

    if (this->chemistry_)
    {
        scalarField& RRpor = tRRpor.ref();

        forAll(Ys_, i)
        {
            forAll(RRpor, cellI)
            {
                scalar rho = solidThermo_[i].rho(T[cellI]);
                RRpor[cellI] -= RRs_[i][cellI]/rho;
            }
        }

    }
    return tRRpor;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::tmp<Foam::DimensionedField<Foam::scalar, Foam::volMesh>>
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::calculateRR
(
    const label ri,
    const label si
) const
{
    tmp<volScalarField::Internal> tRR
    (
        volScalarField::Internal::New
        (
            "RR",
            this->mesh(),
            dimensionedScalar(dimMass/dimVolume/dimTime, 0)
        )
    );



    return tRR;
}


template<class SolidThermo, class SolidThermoType, class GasThermoType>
void Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::calculate()
{

    const volScalarField rho
    (
        IOobject
        (
            "rho",
            this->time().timeName(),
            this->mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
        this->solidThermo().rho()
    );

    resetReactionRates(rho);

    if (this->chemistry_)
    {
        forAll(rho, celli)
        {
            cellCounter_ = celli;

            const scalar delta = this->mesh().V()[celli];

            if (reactingCells_[celli])
            {
                scalar rhoi = rho[celli];
                scalar Ti = this->solidThermo().T()[celli];

                scalarField c(nSpecie_, 0.0);
                loadCellConcentrations
                (
                    celli,
                    rhoi*(1. - porosityF_[celli]),
                    rhoG_[celli]*porosityF_[celli]
                );
                c = specieConcentration_;
                // it is not seen from outside the class so no need to wrap it
                scalarField rR(nReaction_, 0.0);
                const scalarField dcdt = omega(c, Ti, 0.0, rR, true);
                forAll(RRs_, i)
                {
                    RRs_[i][celli] = dcdt[i]/delta;
                }

                forAll(RRg_, i)
                {
                    RRg_[i][celli] = dcdt[nSolids_ + i]/delta;
                }
            }
        }
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::resetReactionRates
(
    const volScalarField& rho
)
{
    if (this->mesh().changing())
    {
        forAll(RRs_, i)
        {
            RRs_[i].setSize(rho.size());
        }
        forAll(RRg_, i)
        {
            RRg_[i].setSize(rho.size());
        }
    }

    forAll(RRs_, i)
    {
        RRs_[i] = 0.0;
    }
    forAll(RRg_, i)
    {
        RRg_[i] = 0.0;
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::scalar
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::solve
(
    const scalar t0,
    const scalar deltaT
)
{
    const volScalarField rho
    (
        IOobject
        (
            "rho",
            this->time().timeName(),
            this->mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
        this->solidThermo().rho()
    );

    volScalarField newDeltaTMin
    (
        IOobject
        (
            "newDeltaTMin",
            this->time().timeName(),
            this->mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        this->mesh(),
        dimensionedScalar("zero", dimless, GREAT)
    );

    resetReactionRates(rho);
    shReactionHeat_ *= 0.0;

    if (!this->chemistry_)
    {
        return GREAT;
    }
    else
    {
        scalar deltaTMin = GREAT;
        newDeltaTMin = GREAT;

        forAll(rho, celli)
        {
            solveOneCell(t0, deltaT, deltaTMin, celli, rho, newDeltaTMin);
        }

        // Doesn't allow the time-step to rise.
        deltaTMin = min(deltaTMin, deltaT);
        reduce(deltaTMin,minOp<scalar>());
        scalar output = gMin(newDeltaTMin);

        return output;
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::solveOneCell
(
    const scalar t0,
    const scalar deltaT,
    scalar& deltaTMin,
    const label celli,
    const volScalarField& rho,
    volScalarField& newDeltaTMin
)
{
    if (reactingCells_[celli])
    {
        cellCounter_ = celli;

        // Initialises properties of the cell.
        scalar solidRho = rho[celli] * (1. - porosityF_[celli]);
        scalar gasRho = rhoG_[celli] * porosityF_[celli];
        scalar Ti = this->solidThermo().T()[celli];

        loadCellConcentrations(celli, solidRho, gasRho);

        const scalarField initialSpecieConcentration(specieConcentration_);

        if (showRRR_)
        {
            scalarField rR(nReaction_, 0.0);
            omega(initialSpecieConcentration, Ti, 0.0, rR);

            if (gSum(rR) > 0)
            {
                Info<< "relative reaction rates: " << rR / gSum(rR) << endl;
            }
        }

        calculateSourceTerms(t0, deltaT, deltaTMin, celli, solidRho, Ti, initialSpecieConcentration);

        updateReactionRates(deltaT, deltaTMin, celli, solidRho, gasRho, initialSpecieConcentration, newDeltaTMin);
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::loadCellConcentrations
(
    const label celli,
    const scalar solidRho,
    const scalar gasRho
)
{
    for (label i = 0; i < nSolids_; ++i)
    {
        specieConcentration_[i] = solidRho * Ys_[i][celli];
    }

    for (label i = 0; i < nGases_; ++i)
    {
        specieConcentration_[nSolids_ + i] = gasRho * gasPhaseGases_[gasDictionary_[i]][celli];
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::calculateSourceTerms
(
    const scalar t0,
    const scalar deltaT,
    scalar& deltaTMin,
    const label celli,
    const scalar solidRho,
    scalar& Ti,
    const scalarField& initialSpecieConcentration
)
{

    scalar t = t0;
    scalar stepHeat = 0.0;

    tauC_ = this->deltaTChem_[celli];
    dt_ = min(deltaT, tauC_);

    scalar timeLeft = deltaT;

    // Calculate the source terms.
    while (timeLeft > SMALL)
    {
        tauC_ = this->solve(specieConcentration_, Ti, 0.0, t, dt_);
        t += dt_;
        stepHeat += subStepHeat_;

        Ti += solidTemperatureIncrement(celli, solidRho, Ti, initialSpecieConcentration);

        timeLeft -= dt_;
        this->deltaTChem_[celli] = tauC_;
        dt_ = min(timeLeft, tauC_);
        dt_ = max(dt_, SMALL);
    }

    deltaTMin = min(tauC_, deltaTMin);

    // Heat from the same integration as the species sources (#209 finding 1).
    if (not solidReactionEnergyFromEnthalpy_)
    {
        shReactionHeat_[celli] = stepHeat/deltaT;
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
Foam::scalar
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::solidTemperatureIncrement
(
    const label celli,
    const scalar solidRho,
    const scalar Ti,
    const scalarField& initialSpecieConcentration
) const
{
    // Total mass density.
    scalar cTot = 0.0;
    for (label i=0; i<nSolids_; i++)
    {
        cTot += specieConcentration_[i];
    }

    scalar newCp = 0.0;
    scalar newhi = 0.0;
    scalar invRho = 0.0;
    scalarList dcdt = (specieConcentration_ - initialSpecieConcentration)/dt_;

    if (solidReactionEnergyFromEnthalpy_)
    {
        for (label i=0; i < nSolids_; i++)
        {
            scalar dYi = dcdt[i];
            scalar Yi = specieConcentration_[i];
            newhi -= dYi * solidThermo_[i].hf();
            newCp += Yi * solidThermo_[i].Cp(Ti);
            invRho += Yi / solidThermo_[i].rho(Ti);
        }
    }
    else
    {
        for (label i=0; i<nSolids_; i++)
        {
            scalar Yi = specieConcentration_[i];
            newCp += Yi * solidThermo_[i].Cp(Ti);
            invRho += Yi / solidThermo_[i].rho(Ti);
        }
        newhi += subStepHeat_/dt_;
    }

    // newCp is volumetric (J/m3/K), so dT = newhi/newCp*dt. A vanishing
    // solid (newCp -> 0) while newhi stays finite would overflow to inf/NaN
    // (FOAM_SIGFPE): with no solid there is no solid temperature to advance.
    scalar dTi = 0;
    if (newhi != 0 && newCp > VSMALL)
    {
        dTi = (newhi/newCp)*dt_;
    }
    else if (newhi != 0)
    {
        // Reports the cell state the first time the guard skips an update,
        // once per process, so that a run can show whether the state occurs
        // at all. Solid mass fractions are listed by name over the solids
        // actually present -- the composition is case-dependent.
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            Pout<< "ODESolidHeterogeneousChemistryModel::calculateSourceTerms:"
                << " skipped solid T update (negligible solid heat capacity)"
                << " cell=" << celli
                << " porosity=" << porosityF_[celli]
                << " solidRho=" << solidRho
                << " newCp=" << newCp
                << " newhi=" << newhi
                << " Ti=" << Ti;

            forAll(Ys_, i)
            {
                Pout<< ' ' << Ys_[i].name() << '=' << Ys_[i][celli];
            }

            Pout<< endl;
        }
    }

    // A heat capacity above VSMALL can still be small enough that the
    // division overflows. Abort at the line that produced it rather than
    // let a non-finite Ts reach SolidStateChecker three calls later.
    if (!std::isfinite(dTi))
    {
        FatalErrorIn("calculateSourceTerms")
            << "Non-finite solid temperature update at cell " << celli
            << ": dTi=" << dTi << " from newhi=" << newhi
            << ", newCp=" << newCp << ", solidRho=" << solidRho
            << ", dt=" << dt_ << ", Ti=" << Ti
            << ", porosity=" << porosityF_[celli] << exit(FatalError);
    }

    return dTi;
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::updateReactionRates
(
    const scalar deltaT,
    scalar& deltaTMin,
    const label celli,
    const scalar solidRho,
    const scalar gasRho,
    const scalarField& initialSpecieConcentration,
    volScalarField& newDeltaTMin
)
{
   const scalarField changeOfConcentration = specieConcentration_ - initialSpecieConcentration;

    updateSolidRates(deltaT, deltaTMin, celli, solidRho, changeOfConcentration, newDeltaTMin);

    updateGasRates(deltaT, deltaTMin, celli, gasRho, changeOfConcentration, newDeltaTMin);
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::updateSolidRates
(
    const scalar deltaT,
    scalar& deltaTMin,
    const label celli,
    const scalar solidRho,
    const scalarField& changeOfConcentration,
    volScalarField& newDeltaTMin
)
{
    forAll(RRs_, i)
    {
        RRs_[i][celli] = changeOfConcentration[i] / deltaT ;

        if ((solidRho * RRs_[i][celli] != 0) && (mag(RRs_[i][celli]) > ROOTVSMALL))
        {
            if (1. <= specieConcentration_[i] / solidRho)
            {
                newDeltaTMin[celli] = min(newDeltaTMin[celli], (1.01 - Ys_[i][celli]) / RRs_[i][celli] * solidRho);
                deltaTMin = min(deltaTMin, newDeltaTMin[celli]);

                if (1.02 < specieConcentration_[i] / solidRho)
                {
                    Info << indent << indent << " too much   " << specieConcentration_[i]/solidRho << " of " << Ys_[i].name() << " in cell " << celli << " limits time step to [s] " << deltaTMin << nl;
                }
            }

            if (0. >= specieConcentration_[i] / solidRho)
            {
                newDeltaTMin[celli] = min(newDeltaTMin[celli], (-.01-Ys_[i][celli])/RRs_[i][celli]*solidRho);
                deltaTMin = min(deltaTMin,newDeltaTMin[celli]);
                if (-0.02 > specieConcentration_[i]/solidRho)
                {
                    Info << indent << indent << " too little " << specieConcentration_[i]/solidRho << " of " << Ys_[i].name() << " in cell " << celli << " limits time step to [s] " << deltaTMin << nl;
                }
            }
            if (deltaTMin < 0)
                Info<< dt_ << " " << deltaTMin << " " << tauC_ << " an error occured: negative deltaT from solid chemistry" << endl;
        }
    }
}

template<class SolidThermo, class SolidThermoType, class GasThermoType>
void
Foam::ODESolidHeterogeneousChemistryModel<SolidThermo, SolidThermoType, GasThermoType>::updateGasRates
(
    const scalar deltaT,
    scalar& deltaTMin,
    const label celli,
    const scalar gasRho,
    const scalarField& changeOfConcentration,
    volScalarField& newDeltaTMin
)
{
    forAll(RRg_, i)
    {
        RRg_[i][celli] = changeOfConcentration[nSolids_ + i] / deltaT;

        if ((changeOfConcentration[nSolids_ + i] * gasRho != 0) && (mag(RRg_[i][celli]) > ROOTVSMALL))
        {
            scalar dtm = deltaTMin;
            if (1. <= specieConcentration_[nSolids_ + i] / gasRho)
            {
                newDeltaTMin[celli] = min(newDeltaTMin[celli], (1.01-gasPhaseGases_[gasDictionary_[i]][celli])/RRg_[i][celli]*gasRho);
                deltaTMin = min(deltaTMin,newDeltaTMin[celli]);
                if (1.02 < specieConcentration_[nSolids_ + i]/gasRho)
                {
                    Info << indent << indent << " too much   " << specieConcentration_[nSolids_ + i]/gasRho << " of " << gasPhaseGases_[gasDictionary_[i]].name() << " in cell " << celli << " limits time step to [s] " << deltaTMin << " " << newDeltaTMin[celli] << nl;
                }
            }
            if (0. >= specieConcentration_[nSolids_ + i] / gasRho)
            {
                newDeltaTMin[celli] = min(newDeltaTMin[celli], (-.01-gasPhaseGases_[gasDictionary_[i]][celli])/RRg_[i][celli]*gasRho);
                deltaTMin = min(deltaTMin,newDeltaTMin[celli]);
                if (-0.02 > specieConcentration_[nSolids_ + i]/gasRho)
                {
                    Info << indent << indent << " too little " << specieConcentration_[nSolids_ + i]/gasRho << " of " << gasPhaseGases_[gasDictionary_[i]].name() << " in cell " << celli << " limits time step to [s] " << deltaTMin << nl;
                }
            }
            if (deltaTMin < 0)
            {
                Info<< dt_  << " " << dtm << " " << deltaTMin << " " << tauC_ << " "
                    << (1.-gasPhaseGases_[gasDictionary_[i]][celli]) << " " << specieConcentration_[nSolids_ + i]/gasRho
                    << " " << gasPhaseGases_[gasDictionary_[i]][celli]  << " an error occured: negative deltaT from solid chemistry" << endl;
            }
        }
    }
}
// ************************************************************************* //
