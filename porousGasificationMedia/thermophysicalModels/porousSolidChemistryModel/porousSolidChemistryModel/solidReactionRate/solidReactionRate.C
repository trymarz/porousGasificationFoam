/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           |
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

#include "solidReactionRate.H"
#include "error.H"

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

Foam::scalar Foam::solidReactionRate::x(const label k, const label celli) const
{
    const label Ns = R_.slhs().size();

    if (k < Ns)
    {
        return f_.Ys[R_.slhs()[k]][celli];
    }
    return f_.gases[R_.glhs()[k - Ns]].internalField()[celli];
}


Foam::scalar Foam::solidReactionRate::rhoBasis(const label celli) const
{
    return R_.slhs().size() > 0 ? f_.rho[celli] : f_.rhoG[celli];
}


bool Foam::solidReactionRate::addDiffusionResistance
(
    const label celli,
    scalar& avKf,
    scalar& agent,
    scalar& omega
) const
{
    const label Ns = R_.slhs().size();
    bool supplied = true;

    forAll(R_.glhs(), g)
    {
        // A negative gas concentration is an invalid state, not a small
        // number: it flips the resistance's sign and 1/avKf then raises the
        // rate instead of bounding it. Abort loudly rather than clamp.
        const scalar gasConc = x(Ns + g, celli);

        if (gasConc < 0)
        {
            FatalErrorInFunction
                << "Negative gas-phase concentration (" << gasConc
                << ") feeding the diffusion-limited rate at cell " << celli
                << ", gas specie index " << R_.glhs()[g]
                << ", reaction:\n" << R_ << exit(FatalError);
        }

        const scalar omegaDiff = f_.ST[celli]*gasConc*f_.rhoG[celli];

        if (omegaDiff != 0)
        {
            // Departs from the literal Eq. 10 (#201): the agent is consumed at
            // xi_a*r, so its resistance is xi_a/Omega_diff, and at the limit the
            // agent consumed equals the Eq. 9 delivery instead of xi_a times it.
            avKf += st_.agent(g)/omegaDiff;
            agent = st_.agent(g);
            omega = omegaDiff;
        }
        else
        {
            supplied = false;
        }
    }

    return supplied;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::solidReactionRate::solidReactionRate
(
    const solidHeterogeneousReaction& R,
    const solidReactionStoichiometry& st,
    const labelUList& gasDictionaryBack,
    const label nSolids,
    const bool diffusionLimited,
    const cellFields& f
)
:
    R_(R),
    st_(st),
    reactants_(R.slhs().size() + R.glhs().size()),
    n_(R.nReact()),
    diffusionLimited_(diffusionLimited),
    f_(f)
{
    const label Ns = R.slhs().size();

    forAll(R.slhs(), s)
    {
        reactants_[s] = R.slhs()[s];
    }
    forAll(R.glhs(), g)
    {
        reactants_[Ns + g] = gasDictionaryBack[R.glhs()[g]] + nSolids;
    }
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::scalar Foam::solidReactionRate::rate
(
    const label celli,
    const scalar T,
    const scalarField& c
) const
{
    scalar kf = R_.kf(T, 0.0, c);

    forAll(reactants_, k)
    {
        kf *= pow(x(k, celli), n_[k]);
    }
    kf *= rhoBasis(celli);

    if (diffusionLimited_ && R_.glhs().size() > 0 && kf > VSMALL)
    {
        scalar avKf = 1./kf;
        scalar agent = 0;
        scalar omega = 0;

        if (addDiffusionResistance(celli, avKf, agent, omega))
        {
            kf = 1./avKf;
        }
        else
        {
            kf = 0;
        }
    }

    return kf;
}


void Foam::solidReactionRate::rateDerivative
(
    const label celli,
    const scalar T,
    const scalarField& c,
    scalarField& dr
) const
{
    scalar kf0Kinetic = R_.kf(T, 0.0, c);
    kf0Kinetic *= rhoBasis(celli);
    kf0Kinetic *= (1. - f_.porosity[celli]);

    forAll(reactants_, j)
    {
        scalar kf = kf0Kinetic;

        forAll(reactants_, i)
        {
            const scalar el = n_[i];

            if (i != j)
            {
                kf *= pow(x(i, celli), el);
            }
            else if (el < 1.0)
            {
                if (c[reactants_[i]] > SMALL)
                {
                    kf *= el*pow(x(i, celli) + VSMALL, el - 1.0);
                }
                else
                {
                    kf = 0.0;
                }
            }
            else
            {
                kf *= el*pow(x(i, celli), el - 1.0);
            }
        }

        // Eq. 10 by the chain rule on 1/r = 1/k0 + agent/Omega, with k0 the
        // kinetic constant without the x factors and only the last supplied
        // gas's term (the existing approximation, see Description).
        if (diffusionLimited_ && R_.glhs().size() > 0 && kf0Kinetic != 0)
        {
            scalar avKf = 1./kf0Kinetic;
            scalar agent = 0;
            scalar omega = 0;

            const scalar kf0 =
                addDiffusionResistance(celli, avKf, agent, omega)
              ? 1./avKf
              : 0.;

            if (kf0Kinetic*omega != 0)
            {
                const scalar chosenKf = agent*f_.ST[celli]*f_.rhoG[celli];

                kf = kf0*kf0
                   *(
                        kf/(kf0Kinetic*kf0Kinetic)
                      + chosenKf/(omega*omega)
                    );
            }
            else
            {
                kf = 0.;
            }
        }

        dr[j] = kf;
    }
}


// ************************************************************************* //
