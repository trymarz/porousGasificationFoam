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

Foam::scalar Foam::solidReactionRate::xBasis(const label k, const label celli)
    const
{
    if (k < R_.slhs().size())
    {
        return f_.rho[celli]*(1. - f_.porosity[celli]);
    }
    return f_.rhoG[celli]*f_.porosity[celli];
}


Foam::scalar Foam::solidReactionRate::x
(
    const label k,
    const label celli,
    const scalarField& c
) const
{
    const scalar basis = xBasis(k, celli);

    return basis > 0 ? max(c[reactants_[k]], 0.)/basis : 0.;
}


Foam::scalar Foam::solidReactionRate::rhoBasis(const label celli) const
{
    return R_.slhs().size() > 0 ? f_.rho[celli] : f_.rhoG[celli];
}


bool Foam::solidReactionRate::addDiffusionResistance
(
    const label celli,
    const scalarField& c,
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
        const scalar gasConc = x(Ns + g, celli, c);

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
        kf *= pow(x(k, celli, c), n_[k]);
    }
    kf *= rhoBasis(celli);

    if (diffusionLimited_ && R_.glhs().size() > 0 && kf > VSMALL)
    {
        scalar avKf = 1./kf;
        scalar agent = 0;
        scalar omega = 0;

        if (addDiffusionResistance(celli, c, avKf, agent, omega))
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
    const label Ns = R_.slhs().size();
    const scalar k0 = R_.kf(T, 0.0, c)*rhoBasis(celli);
    const scalar pf = 1. - f_.porosity[celli];

    scalar rKin = k0;
    forAll(reactants_, i)
    {
        rKin *= pow(x(i, celli, c), n_[i]);
    }

    // Eq. 10: 1/r = 1/rKin + sum agent/Omega, so dr = r^2 (drKin/rKin^2
    // + agent_j/(ST rhoG x_j^2) for a gas reactant j).
    const bool limited = diffusionLimited_
        && R_.glhs().size() > 0 && rKin > VSMALL;

    scalar r = rKin;
    bool supplied = true;
    if (limited)
    {
        scalar avKf = 1./rKin;
        scalar agent = 0;
        scalar omega = 0;
        supplied = addDiffusionResistance(celli, c, avKf, agent, omega);
        r = supplied ? 1./avKf : 0.;
    }

    forAll(reactants_, j)
    {
        const scalar xj = x(j, celli, c);
        const scalar basis = xBasis(j, celli);
        scalar dKin = k0;

        forAll(reactants_, i)
        {
            const scalar el = n_[i];

            if (i != j)
            {
                dKin *= pow(x(i, celli, c), el);
            }
            else if (el < 1.0)
            {
                dKin *= xj > SMALL ? el*pow(xj + VSMALL, el - 1.0) : 0.;
            }
            else
            {
                dKin *= el*pow(xj, el - 1.0);
            }
        }

        scalar dx = dKin;
        if (limited)
        {
            dx = 0;
            if (supplied)
            {
                dx = r*r*dKin/(rKin*rKin);

                if (j >= Ns)
                {
                    dx += r*r*st_.agent(j - Ns)*f_.ST[celli]*f_.rhoG[celli]
                        /sqr(f_.ST[celli]*xj*f_.rhoG[celli]);
                }
            }
        }

        dr[j] = basis > 0 ? pf*dx/basis : 0.;
    }
}


// ************************************************************************* //
