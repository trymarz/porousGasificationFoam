#ifndef LambdaDotCalculationModel_H
#define LambdaDotCalculationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for the DEM particle length-scale rate lambdaDot [m/s].
// Owned by volPyrolysis, which selects it from constant/lambdaDict
// (lambdaMode: constant | exactDifferential) and drives it in
// solveSpeciesMass(): calculateTemperatureDriven() once per step before the
// species loop, then calculateChemistryDriven() per solid specie inside it.
// The model coefficients are read from the same dictionary; lambdaDict also
// carries the unrelated DEM->CFD velocity- and lambda-interpolation entries
// consumed by lambdaDotModel.
//
// PGF owns only this rate. The integrated lambda is owned by YADE
// (State::lambda_, advanced every DEM step) and travels back to the mesh
// through lambdaDotModel's interpolation, so no model here reads lambda.
class LambdaDotCalculationModel
{
protected:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;

public:
    LambdaDotCalculationModel
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~LambdaDotCalculationModel() = default;

    static autoPtr<LambdaDotCalculationModel> New
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    //- Temperature-driven part — called once per step before the species
    //  loop. Sets (overwrites) lambdaDot_ with the current temperature-driven
    //  contribution.
    virtual void calculateTemperatureDriven() = 0;

    //- Chemistry-driven part — called per solid specie inside the species
    //  loop. Adds to lambdaDot_ the contribution of that specie's mass rate
    //  sRhoSi [kg/m^3/s]. specieName is the bare solidComponents name (e.g.
    //  "char", not the field name "Ychar"), so a model can resolve a
    //  per-specie coefficient. Default empty: only exactDifferentialLambdaDot
    //  overrides it.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const word& specieName
    )
    {}

    //- Fraction of the chemistry-driven specie mass change redirected into
    //  the particle length scale (the remaining 1 - fraction becomes pore
    //  space). volPyrolysis uses this to reduce the porosity source in step
    //  with the lambda contribution added by calculateChemistryDriven(), so
    //  the split is conservative regardless of the selected mode. Default 0:
    //  the mass change goes entirely to porosity. Only
    //  exactDifferentialLambdaDot overrides it.
    virtual scalar chemistryMassSplit() const
    {
        return 0.0;
    }
};

} // namespace Foam

#endif
