# Installing porousGasificationFoam

This page takes a clean Debian 13 or Ubuntu 24.04 machine to a running PGF on OpenFOAM v2606, with the optional DEM coupling to YADE. Steps 1 and 2 are all you need without DEM. To run on a server without building there, use an [Apptainer image](containers.md) instead.

## 1. OpenFOAM v2606

PGF builds on ESI's OpenFOAM v2606 (openfoam.com) and nothing else.

**From ESI's apt repository** (recommended). `openfoam2606-dev` brings OpenFOAM with its headers and `wmake`, the compiler and the system Open MPI:

```bash
sudo apt-get install curl ca-certificates git python3
curl -s https://dl.openfoam.com/add-debian-repo.sh | sudo bash
sudo apt-get install openfoam2606-dev
```

It installs to `/usr/lib/openfoam/openfoam2606`; `pgf setup` finds it there. To check it:

```bash
source /usr/lib/openfoam/openfoam2606/etc/bashrc && foamVersion   # OpenFOAM-v2606
```

**From source**, if ESI has no package for your system: follow ESI's build guide for v2606 (OpenFOAM-v2606 and ThirdParty-v2606 sources, `./Allwmake -j`). Then name its `etc/bashrc` once with `./pgf setup --foam-bashrc <OpenFOAM-v2606>/etc/bashrc` in step 2.

## 2. PGF

```bash
git clone https://github.com/trymarz/porousGasificationFoam.git
cd porousGasificationFoam
./pgf setup --yes      # finds OpenFOAM, links pgf into ~/.local/bin
pgf build              # libraries and solver
```

If `pgf setup` warns that `~/.local/bin is not on your PATH` (a new account: Debian and Ubuntu add it only at the next login once it exists), run `export PATH="$HOME/.local/bin:$PATH"` first.

No submodule is needed without DEM. `pgf doctor` checks the whole setup ([`pgf help doctor`](pgf-reference.md#pgf-help-doctor)); then copy and run a first tutorial, steps 3 and 4 of the [README quick start](../README.md#quick-start). What setup saves and where: [the pgf tool → Settings](pgf.md#settings).

## 3. DEM coupling (optional)

DEM cases need YADE from [trymarz/yade-pgf](https://github.com/trymarz/yade-pgf) (branch `pgf-main`: YADE 2026.1.0 plus the per-particle `lambda` state and the `PgfCoupling` engine), at the commit PGF pins. Upstream YADE does not work: the per-particle state and the engine exist only in the fork. How the pin and the protocol work: [the build system → YADE DEM coupling](build.md#yade-dem-coupling).

**System packages.** YADE's build dependencies, the list `pgf pack` installs (`PGF_PACK_YADE_DEPS` in `utilities/pgf/pack.sh`):

```bash
sudo apt-get install cmake git g++ python3-dev python3-numpy python3-mpi4py python3-mpmath python3-matplotlib python3-ipython python3-pygraphviz python3-xlib python3-sphinx python3-tk libboost-all-dev libeigen3-dev libgts-dev libcgal-dev libvtk9-dev libsqlite3-dev libbz2-dev zlib1g-dev libopenblas-dev libsuitesparse-dev libmetis-dev coinor-libclp-dev libmpfr-dev libmpfrc++-dev libmpc-dev
```

On Debian 13, `libvtk9-dev` cannot be installed beside `python3-paraview` ([ParaView and Yade's VTK](#paraview-and-yades-vtk)).

**yade-pgf at the pin.** The pin is the submodule `submodules/yade-pgf`. It carries the coupling core as its own submodule (`extra/PgfDemBridge`), and PGF's DEM build needs `submodules/PgfDemBridge`, so check out both, then the nested bridge (not `--recursive`: yade-pgf's `data` submodule is private):

```bash
git submodule update --init submodules/yade-pgf submodules/PgfDemBridge
git -C submodules/yade-pgf submodule update --init extra/PgfDemBridge
```

To build from a yade-pgf clone of your own instead, check out the pinned commit (`git ls-tree HEAD submodules/yade-pgf` prints it) and pass `--yade-trunk DIR` to `pgf setup` below.

**Build YADE.** The CMake line is the one `pgf pack` uses (`utilities/pgf/pack.sh`). yade-pgf ships a `RELEASE` file, so without `-DYADE_VERSION` its version is plain `2026.1.0`; the line puts the commit into the version, where `pgf doctor` reads it:

```bash
src="$PWD/submodules/yade-pgf"
ver="$(git -C "$src" show -s --format=%cd --date=format:%Y.%m.%d HEAD)-git-$(git -C "$src" rev-parse --short=7 HEAD)"
mkdir -p ~/build/yade-pgf && cd ~/build/yade-pgf
cmake "$src" -DCMAKE_INSTALL_PREFIX=$HOME/opt/yade-pgf -DENABLE_GUI=OFF -DNOSUFFIX=ON \
    -DYADE_VERSION="$ver" -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)" install      # 30-60 min
```

With less than about 4 GB of memory per core, lower `-j` ([Pitfalls](#pitfalls)). Run it in a plain shell, not one that has sourced OpenFOAM: YADE and OpenFOAM must use the same MPI, the system Open MPI. CMake must not print `MPI NOT FOUND, YADE-OpenFOAM coupling disabled`. `$HOME/opt/yade-pgf/bin/yade -v` then prints `Yade version: <date>-git-<commit>`.

**Build and check the DEM variant of PGF.** Back in the PGF checkout:

```bash
./pgf setup --yade-bin $HOME/opt/yade-pgf/bin/yade
pgf build --yade
pgf doctor --yade      # yade at the pin, with MPI; DEM problems are failures here
```

**Run a DEM tutorial.** `pgf run` gives a copy with an active `constant/yadeProperties` the DEM build by itself. Its `Allrun` starts 2 YADE and 2 solver ranks in one `mpirun` (`pgf dem run`); the run takes a few minutes:

```bash
pgf copy MicroTGA-DEM_lambdaDotOff
pgf run ~/OpenFOAM/$USER-v2606/run/main/MicroTGA-DEM_lambdaDotOff
```

The run's output, kept in the case's `log.yade`, shows the solver's `DEM coupling: active` and the handshake on both sides, `pgfDemCoupler: handshake ok, protocol v2` and `PgfCoupling: handshake ok, protocol v2`; [Checking that the coupling is in](#checking-that-the-coupling-is-in) lists the other checks.

## Checking that the coupling is in

- **Build variant.** A `--yade` solver links `liblambdaDotModel` (which holds `pgfDemCoupler`). In a normal build it does not appear:

  ```bash
  ldd $(which porousGasificationFoam) | grep liblambdaDotModel
  ```

  It must resolve to a path; `not found` means the directory holding them is not on `LD_LIBRARY_PATH`. For a prefix build, `.pgf-build.json` in the prefix records `"variant": "yade1"`.
- **YADE side.** `yade --help` ends its description with the compiled features; the list must contain `MPI`.
- **At run time.** A `WITH_YADE` solver prints `DEM coupling: active` (or `inactive (yadeProperties absent or active=false)`) at startup, from `createDEMFields.H`; a normal build prints no such line. `pgf dem run` prints `pgf dem run: N Yade + M OpenFOAM rank(s)` before its `mpirun`. The solver always runs with `-parallel`, so the case must be decomposed (into one `processor0` for one OpenFOAM rank).
- **Handshake.** Once both sides agree, the solver prints `pgfDemCoupler: handshake ok, protocol v2, lambdaMode …, N DEM rank(s)` and YADE prints `PgfCoupling: handshake ok, protocol v2, lambdaMode …, M OpenFOAM rank(s)`; a tutorial's `Allrun` keeps both in `log.yade`. A mismatch stops the run with a message naming both versions. A DEM script without `PgfCoupling` leaves the solver waiting in the handshake; `runDEMCase.sh`'s timeout ends such a run.
- **End.** OpenFOAM's last step ends YADE: it prints `PgfCoupling: OpenFOAM ran its last step; the DEM run ends at iter N`. A YADE run that ends first stops OpenFOAM.

## ParaView and Yade's VTK

YADE is configured with `ENABLE_VTK=ON` for `VTKRecorder` (C++, in `libpost_vtk.so`), which DEM cases such as `MicroTGA-DEM_lambdaDot_common` use for their sphere output. On Debian 13 that means two packages with different jobs:

| Package | Needed for |
|---|---|
| `libvtk9-dev` (depends on `python3-vtk9`) | configuring and compiling YADE (`VTK_DIR=/usr/lib/x86_64-linux-gnu/cmake/vtk-9.3`, the headers) |
| `libvtk9.3` | running YADE: the `libvtk*-9.3.so.1` that `libyade.so`, `libpost_vtk.so` and others link |

Neither YADE's Python modules nor PGF's DEM scripts import the `vtk` Python module: `yade.export.VTKExporter` and the cases' `MPI_lambda.py` write VTK files as text. `pgf build --yade` does not need VTK either.

`python3-paraview`, which [`pgf view`](paraview.md) needs, conflicts with `python3-vtk9` (both ship the `vtkmodules` Python package), so `libvtk9-dev` and `python3-paraview` cannot be installed together. The route that works:

1. Day to day, keep `paraview python3-paraview` and the runtime `libvtk9.3`. `libvtk9.3` is usually marked as automatically installed, so keep `apt autoremove` from taking it: `sudo apt-mark manual libvtk9.3`.
2. To build or rebuild YADE (for example when the pin moves), swap: `sudo apt install libvtk9-dev` (removes `python3-paraview`), build YADE, then `sudo apt install python3-paraview` (removes `libvtk9-dev` and `python3-vtk9` again). The installed YADE keeps working; `ldd <yade lib dir>/libpost_vtk.so | grep 'not found'` should print nothing.
3. If you need both at once, use a Kitware ParaView binary (its own Python and VTK, no apt packages) with its `bin/` first on `PATH`, and keep `libvtk9-dev`.

`pgf doctor` reports, under "ParaView", whether the ParaView on your `PATH` has Python.

## Pitfalls

- **One MPI.** YADE and the solver run in one `mpirun`, so YADE, `mpi4py` and OpenFOAM must use the same Open MPI. Build YADE against the system Open MPI that `openfoam2606-dev` uses, not a ThirdParty or conda one.
- **Memory for the YADE build.** Each `make` job needs roughly 3–4 GB; pick `-j` from the free memory, not the core count, or `cc1plus` is killed. Rerunning `make` resumes.
- **YADE off the pin.** After a `git pull` that moves `submodules/yade-pgf`, rerun the two `git submodule update` lines above (without `--init`) and rebuild YADE; `pgf doctor --yade` names the commits when they differ.
- **Single-interface hosts.** On a host whose only network is `lo`, or with docker or libvirt bridges, Open MPI can hang the coupled run; `pgf` then restricts it to loopback by itself (`PGF_MPI=auto`; [`pgf help config`](pgf-reference.md#pgf-help-config)).

## Related pages

- [README quick start](../README.md#quick-start)
- [The pgf tool](pgf.md), [Apptainer images](containers.md)
- [The build system](build.md): how builds work, for changing the code
- [All documentation](README.md)
