#ifndef LambdaDotCalculationModel_H
#define LambdaDotCalculationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for the DEM particle length-scale rate lambdaDot
// [dimensionless in the registry — see createDEMFields.H]. Owned by
// volPyrolysis, which selects it from constant/lambdaDict
// (lambdaMode: none | constant | Ts | exactDifferential) and drives it in
// solveSpeciesMass(): calculateTemperatureDriven() once per step before the
// species loop, then calculateChemistryDriven() per solid specie inside it.
// The model coefficients are read from the same dictionary; lambdaDict also
// carries the unrelated DEM->CFD velocity-interpolation entries consumed by
// lambdaDotModel.
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
    //  loop. Adds to lambdaDot_ the per-specie mass-change contribution.
    //  Default empty: only exactDifferentialLambdaDot overrides it.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const volScalarField& rho,
        const volScalarField& lambda
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
