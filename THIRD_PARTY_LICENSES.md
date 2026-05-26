# Third-Party Licenses

## Foam-Yade (`submodules/foam-yade/`)

| | |
|---|---|
| **Repository** | https://github.com/zhiwar-ep/Foam-Yade |
| **Upstream** | https://gitlab.com/yade-dev/trunk |
| **License** | GNU General Public License v2.0 or later (GPL-2.0+) |
| **License file** | `submodules/foam-yade/LICENSE` |

Foam-Yade is a modified version of the Yade DEM framework adapted for
OpenFOAM–Yade coupling, including `lambda`/`lambdaDot` data exchange between
OpenFOAM and Yade.  The modifications were made by contributors to this
project and are distributed under the same GPL-2.0+ terms as the original.

### License compatibility with porousGasificationFoam (GPL-3.0)

Yade and Foam-Yade are licensed under GPL **v2.0 or later**, which includes
the option to use the code under GPL-3.0.  porousGasificationFoam is licensed
under GPL-3.0.  The two are compatible: the "or later" clause of Foam-Yade
allows it to be treated as GPL-3.0 for the purposes of combination.

At runtime the coupling is MPI-based — Yade and OpenFOAM run as **separate
processes** — so there is no compile-time linking between the two code bases
and no license interaction beyond the submodule boundary itself.

### Obligations when distributing

If you distribute porousGasificationFoam together with the Foam-Yade
submodule (or any derivative of it), you must:

1. Preserve all copyright notices in `submodules/foam-yade/`.
2. Carry prominent notices in any modified files stating that you changed
   them and the date of the change (GPL-2.0+ §5(a)).
3. Make the complete corresponding source available under GPL-2.0+ terms.
4. Include the GPL-2.0 license text (`submodules/foam-yade/LICENSE`).
