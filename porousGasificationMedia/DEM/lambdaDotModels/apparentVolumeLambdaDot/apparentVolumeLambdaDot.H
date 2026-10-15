#ifndef apparentVolumeLambdaDot_H
#define apparentVolumeLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "volFields.H"

namespace Foam
{

// Particle length-scale update from the apparent (envelope) volume the solid
// species occupy, selected with lambdaMode apparentVolume in constant/lambdaDict.
//
// Every solid specie counts as structural. With rhoApparent_i the apparent
// density of one particle [kg/m^3], including its internal pores, and
// lam3_i = 1/rhoApparent_i [m^3/kg]:
//
//   V_old   = sum_i Ym_i*lam3_i                   Ym_i before this step's solve
//   dV_chem = deltaT*sum_i sRhoS_i*lam3_i         chemistry only, no transport
//   r       = cbrt(1 + dV_chem/V_old)
//
// V_old is the apparent particle volume per unit cell volume [-]. Only ratios
// of lam3_i matter: scaling every rhoApparent_i by one constant cancels in r.
// Transport (div(phiSolid*Ym), bed collapse) is left out on purpose, since it
// moves particles and would otherwise inflate or shrink them.
//
// Unlike the other models this one sends r [-], not a rate [m/s]. YADE
// multiplies State::lambda_ by r once per exchange. The model therefore turns
// lambdaDot_ dimensionless, with neutral value 1 (no change), and writes r into
// it every step. Every particle in a cell gets the same cell-mean r, even when
// a wood and a char particle share the cell.
//
// Guards: r = 1 where V_old < minSolidVolume (nearly empty cell); where
// 1 + dV_chem/V_old <= minVolumeRatio (full burnout) the ratio is clamped to
// minVolumeRatio and a warning is logged. Both are optional lambdaDict keys.
class apparentVolumeLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    //- Below this V_old [-] a cell is treated as empty and gets r = 1.
    scalar minSolidVolume_;

    //- Floor on 1 + dV_chem/V_old [-], applied before the cube root.
    scalar minVolumeRatio_;

    //- solidComponents names and their lam3 [m^3/kg], set after construction.
    wordList specieNames_;
    scalarList lam3_;

    //- Per-cell accumulators over the species loop, zeroed by endStep().
    scalarField Vold_;
    scalarField dVchem_;

    //- lam3 [m^3/kg] of one specie; aborts if the name or the list is missing.
    scalar lam3For(const word& specieName) const;

public:
    apparentVolumeLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~apparentVolumeLambdaDot() = default;

    //- No temperature term: shrinkage follows mass and apparent density only.
    virtual void calculateTemperatureDriven() override;

    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const volScalarField& Ym_i,
        const word& specieName
    ) override;

    //- Writes r into lambdaDot_ for every cell and zeroes the accumulators.
    virtual void endStep() override;

    virtual void setApparentSpecificVolumes
    (
        const wordList& specieNames,
        const scalarList& lam3
    ) override;
};

} // namespace Foam

#endif
