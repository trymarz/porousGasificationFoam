# **porousGasificationFoam**

A comprehensive **OpenFOAM solver** for thermal and chemical conversion in porous media, including pyrolysis, gasification, and combustion with coupled fluid flow and conjugate heat transfer.

| | |
|---|---|
| **Version** | OpenFOAM-v2406 (ESI Community) |
| **License** | GNU GPL v3 |
| **Status** | Published |
| **Citation** | [Zuk et al., Computer Physics Communications (2025)](#citation) |

---

## Quick Navigation

- [Features](#features)
- [System Requirements](#system-requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Tutorial Cases](#tutorial-cases)
- [Common Pitfalls](#common-pitfalls)
- [Project Structure](#project-structure)
- [Documentation](#documentation)
- [Citation & References](#citation--references)
- [Support](#support)

---

## Features

**porousGasificationFoam** is a comprehensive CFD tool built on **OpenFOAM-v2406** for simulating reactive flow in porous media. It solves coupled equations for solid and gas phases with full energy balance and chemical kinetics:

| Feature | Description |
|---------|-------------|
| **Dual-phase modeling** | Independent solid and gas phase energy balance for out-of-equilibrium calculations |
| **Darcy-Forchheimer flow** | Resistance modeling in porous media via Darcy coefficient (Df) and Forchheimer coefficient (Fc) |
| **Heterogeneous reactions** | Arbitrary solid-phase kinetic reactions with Arrhenius temperature dependence |
| **Homogeneous reactions** | Gas-phase chemistry via ODE solvers (fast compared to slow solid reactions) |
| **Convective heat transfer** | Solid-gas phase heat exchange with customizable correlations |
| **Radiative heat transfer** | Volumetric heterogeneous radiation model (P1 or mean temperature approximation) |
| **Immersed boundary method** | For solid enthalpy equation without explicit solid domain boundaries |
| **Optional Yade integration** | Discrete element method (DEM) for particle-scale coupling ([read more](#yade-integration)) |
| **Parallel computing** | Domain decomposition with MPI support |
| **Multi-species transport** | Gas and solid species with diffusion and mass transfer limitations |

---

## System Requirements

### Required

- **OpenFOAM-v2406** (ESI Community version)  
  Installation guide: <https://openfoam.org/version/2406/>

- **Ubuntu 22.04 LTS** (tested; other Linux distributions likely compatible)

- **Build tools**: gcc/g++, make, CMake

- **Standard libraries**: OpenFOAM dependencies (installed automatically with OpenFOAM)

### Optional

- **Yade DEM** (for particle-coupling simulations)  
  Installation: <https://yade-dem.org/doc/installation.html>

- **ParaView** (for visualization)  
  <https://www.paraview.org/>

- **Doxygen + Graphviz** (for generating code documentation)  

  ```bash
  sudo apt-get install doxygen graphviz
  ```

---

## Installation

### Step 1: Set Up OpenFOAM Environment

Source the OpenFOAM bashrc file (adjust path if installed elsewhere):

```bash
source /opt/OpenFOAM/OpenFOAM-v2406/etc/bashrc
```

Verify the installation:

```bash
icoFoam -help
echo $WM_PROJECT_USER_DIR  # Should print a path, e.g., /home/user/OpenFOAM/user-v2406
```

### Step 2: Clone Repository

```bash
cd $WM_PROJECT_USER_DIR
git clone https://github.com/trymarz/porousGasificationFoam.git
cd porousGasificationFoam
```

### Step 3: Build Installation Environment

Set the installation package environment:

```bash
source porousGasificationMediaDirectories
```

(Optional: To install elsewhere, edit `porousGasificationMediaDirectories` before sourcing)

### Step 4: Compile Solver & Library

```bash
./Allwmake
```

To see detailed output and save logs:

```bash
./Allwmake > log.make 2>&1 &
tail -f log.make
```

### Step 5: Verify Installation

Run the solver with no arguments (should display usage info):

```bash
porousGasificationFoam
```

You should see usage output and no errors.

---

## Quick Start

### For Experienced OpenFOAM Users

**1. Copy a tutorial case:**

```bash
cd $WM_PROJECT_USER_DIR/porousGasificationFoam/tutorials
cp -r macroTGA_688K ~/myCase
cd ~/myCase
```

**2. Review case structure:**

Standard OpenFOAM directories: `0/`, `constant/`, `system/`  
PGF-specific dictionaries in `constant/`:

- `pyrolysisProperties` — pyrolysis model & heat transfer settings
- `solidThermophysicalProperties` — solid species & their thermodynamics
- `chemistryProperties` — gas & heterogeneous reactions
- `radiationProperties` — radiation model & parameters
- `heatTransferProperties` — convective heat transfer correlations
- `porosityProperties` — Darcy/Forchheimer coefficients (optional)
- `specieTransferProperties` — mass transfer (if diffusion-limited)

**3. Edit for your case** (mesh, BCs, properties, reactions, etc.)

**4. Run:**

```bash
porousGasificationFoam > log &
```

Or parallel (adjust `numberOfSubdomains` in `system/decomposeParDict`):

```bash
decomposePar
mpirun -np 4 porousGasificationFoam -parallel > log &
```

---

## Workflow Overview

### Preprocessing

1. **Generate mesh** using OpenFOAM tools:
   - `blockMesh` — structured hexahedral mesh
   - `snappyHexMesh` — automatic mesh with geometry
   - External tools: Salome, Blender → STL → OpenFOAM

2. **Set initial/boundary conditions** in `0/` directory:
   - `p` — pressure
   - `U` — velocity
   - `T` — gas temperature
   - `Ts` — solid temperature
   - `Yi` — gas species mass fractions
   - `Ys` — solid species fractions

3. **Define domain properties** in `constant/`:
   - Mesh: `polyMesh/`
   - Thermophysical properties (gas: `thermo.compressibleGas`)
   - Transport coefficients, reactions, radiation

4. **Configure solver settings** in `system/`:
   - `controlDict` — time stepping, output, stop time
   - `fvSchemes` — discretization schemes
   - `fvSolution` — linear solvers & tolerances

### Running

```bash
# Sequential
porousGasificationFoam > log &

# Parallel
decomposePar
mpirun -np N porousGasificationFoam -parallel > log &
```

### Postprocessing

**Built-in utility for solid mass:**

```bash
totalMassPorousGasificationFoam
```

Outputs `totalMass.txt` (time vs. integrated solid mass).

**General visualization & analysis:**

- ParaView (native OpenFOAM support)
- OpenFOAM post-processing utilities (`postProcess`, `foamLog`, etc.)
- Runtime processing with `controlDict` entries

---

## Tutorial Cases

The `tutorials/` directory contains **5 validation cases** with experimental data:

| Case | Description | Physics | Key Focus |
|------|-------------|---------|-----------|
| **macroTGA_688K** / **879K** | Wooden ball pyrolysis in tube furnace | Thermal gradients, kinetics | Macro-scale pyrolysis |
| **microTGA** | Small wooden particle pyrolysis | Drying + pyrolysis | Kinetic-limited regime |
| **biomassPressureDrop** | Flow resistance through porous bed | Darcy-Forchheimer | Permeability validation |
| **flatPlate** | Porous flat plate in cross-flow | Momentum & heat transfer | Immersed boundary method |
| **gasifier** | Axisymmetric packed-bed gasifier | Full gasification | Industrial-scale (diffusion-limited) |

**Run a tutorial:**

```bash
cd tutorials/macroTGA_688K
./buildCase688.sh      # Preprocesses mesh & fields
porousGasificationFoam # Run solver
```

See included README in each case directory for details.

---

## Common Pitfalls

> ⚠️ **These are frequent issues reported by users. Review before running your first case.**

### 1. **Solid Properties Must Be "True" Density, Not Bulk**

**Problem:** Users specify bulk/apparent density of the porous material instead of pure solid density.

**Solution:** Use the **true density** of the solid material (porosityF = 0 in calculations). For wood:

- **True density:** ~1000–1500 kg/m³
- **Bulk density:** (1 − ε) × ρ_true (ε = porosity/void fraction)

Example in `solidThermophysicalProperties`:

```c++
woodCoeffs
{
  density
  {
    rho 1050;  // TRUE density of wood, not bulk!
  }
  // ... heat capacity, thermal conductivity, etc.
}
```

### 2. **JANAF Thermodynamic Data May Not Exist for Pseudo-Species**

**Problem:** Pyrolysis produces "pseudo-gases" (targas, volatiles) that don't have standard thermodynamic data.

**Solution:**

- Use an existing gas species that closely mimics the pseudo-species (e.g., use `C2H6` data for `targas`)
- Edit `constant/thermo.compressibleGas` to add mimicked properties
- See manual section 3.3.4 for details

### 3. **Time Step Too Large for Gas Reactions**

**Problem:** Gas-phase reactions are **orders of magnitude faster** than solid pyrolysis. Large time steps cause solver instability.

**Solution:**

- Start conservatively: Δt ~ 1e-4 to 1e-3 s
- Adjust `initialChemicalTimeStep` in `chemistryProperties`
- Monitor: `solidChemistryTimeStepControl true` allows adaptive sub-stepping
- **Expect long run times** for slow processes like gasification (hours to days)

### 4. **Radiation Properties Are Difficult to Calibrate**

**Problem:** Radiation parameters (absorptivity, emissivity, penetration depth) are hard to find in literature and strongly affect temperature profiles.

**Solution:**

- Start with rough estimates from literature
- **Calibrate against experimental data** (temperature rise rate, final temperatures)
- Use the tuned parameters for similar systems
- See `radiationProperties` in tutorial cases for working examples

### 5. **Biomass Distribution & Porosity Field Setup**

**Problem:** Initial porosity field (solid region) is set incorrectly or incompletely.

**Solution:**

- Use `setFields` + `setSet` for simple geometries:

  ```bash
  setSet -batch setSet.c0   # Define zones
  setFields                  # Assign porosityF values
  ```

- For complex geometries: Create STL in Blender/Salome, use with `setSet`
- Ensure `porosityF = 1` in gas regions (no solids), `porosityF < 1` in biomass regions
- Use `setPorosity` tool for advanced distributions (requires compilation)

### 6. **Initial/Boundary Conditions for Temperature & Species**

**Problem:** Inconsistent or missing initial conditions for `Ts` (solid temperature), `T` (gas temperature), or `Ys` (solid species).

**Solution:**

- Always provide: `0/T`, `0/Ts`, `0/Ys*` (solid species fields)
- If a field is not explicitly written in `0/`, it will be created from defaults (check dictionaries)
- Use `setFields` for spatially varying initial conditions
- Ensure boundary conditions match the physics (e.g., heating at inlet/walls)

### 7. **Mesh Independence & Courant Number**

**Problem:** Results are mesh-dependent; solver diverges with coarse mesh.

**Solution:**

- Run a few cases with increasing mesh resolution
- Monitor Courant number: `Co = U * Δt / Δx` should be < 0.5 for stability
- Use `Allrun` scripts provided in tutorials; they include mesh refinement
- Coarser meshes → smaller time steps needed

---

## Project Structure

```
porousGasificationFoam/
├── README.md                          # This file
├── LICENSE
├── porousGasificationMediaDirectories # Environment setup (edit if custom path)
├── Allwmake                           # Build script
├── Allclean                           # Clean build
│
├── porousGasificationFoam/            # Main solver
│   ├── Make/
│   ├── porousGasificationFoam.C
│   └── ...
│
├── porousGasificationMedia/           # Core library
│   ├── pyrolysisModels/              # Solid phase kinetics & state
│   ├── thermophysicalModels/         # Thermo & chemistry for solids
│   ├── porosityModels/               # Mechanical properties (Darcy, Forchheimer)
│   ├── radiationModels/              # Heterogeneous radiation
│   └── ...
│
├── utilities/                         # Post-processing tools
│   ├── setPorosity/                  # Advanced porosity field setup
│   └── totalMassPorousGasificationFoam/  # Integrate solid mass
│
├── tutorials/                         # Validation cases
│   ├── macroTGA_688K/
│   ├── macroTGA_879K/
│   ├── microTGA/
│   ├── biomassPressureDrop/
│   ├── flatPlate/
│   └── gasifier/
│
└── doc/                               # Documentation
    └── Doxygen/                       # Source code docs (build with Doxygen)
```

---

## Documentation

### Scientific Background

For detailed physics and modeling equations, refer to the **published paper**:

> **Zuk, P.J., Tużnik, B., Rymarz, T., et al.** (2025). *OpenFOAM solver for thermal and chemical conversion in porous media.* **Computer Physics Communications**.  
> DOI: [10.1016/j.cpc.2025.xxxxx](#) [*pending final publication details*]

### Code Documentation (Doxygen)

Generate API documentation from source code:

```bash
cd doc/Doxygen
./Allwmake
open ../../../doc/Doxygen/html/index.html  # View in browser
```

### Tutorials & User Manual

See `tutorials/*/README.md` for case-specific details and setup instructions.

### OpenFOAM Resources

- **Official documentation:** <https://openfoam.org/documentation/>
- **User guide:** <https://www.openfoam.com/documentation/user-guide/>
- **Mesh generation:** <https://openfoam.org/features/meshing/>

---

## Citation & References

### Main Publication

**[PLACEHOLDER - Please provide updated citation once published]**

If you use **porousGasificationFoam** in your research, please cite:

```bibtex
@article{Zuk2
