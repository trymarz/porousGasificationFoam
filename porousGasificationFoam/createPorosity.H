/*---------------------------------------------------------------------------*\
Global
    createPorosity

Description
    Construct the porous-zone container that owns the Darcy/Forchheimer
    resistance terms added to UEqn in the momentum predictor.

    \c fieldPorosityModel is the PGF-specific porous-zone implementation
    (see porousGasificationMedia/fieldPorosityModel). It reads the
    Forchheimer scalar from \c constant/porosityProperties (defaulting
    to zero if the file is absent), and applies the resistance to UEqn
    via \c pZones.addResistance(UEqn, Df) — see UEqn.H.

\*---------------------------------------------------------------------------*/

Info<< "Creating porous zones" << nl << endl;

fieldPorosityModel pZones(mesh, porosityF);
Switch pressureImplicitPorosity(false);
