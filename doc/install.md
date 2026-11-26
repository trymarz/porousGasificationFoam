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
pgf build              # libraries and solver, about 1.5 min
```

If `pgf setup` warns that `~/.local/bin is not on your PATH` (a new account: Debian and Ubuntu add it only at the next login once it exists), run `export PATH="$HOME/.local/bin:$PATH"` first.

No submodule is needed without DEM. `pgf doctor` checks the whole setup ([`pgf help doctor`](pgf-reference.md#pgf-help-doctor)); then copy and run a first tutorial, steps 3 and 4 of the [README quick start](../README.md#quick-start). What setup saves and where: [the pgf tool → Settings](pgf.md#settings).

## 3. DEM coupling (optional)

DEM cases need YADE from [trymarz/yade-pgf](https://github.com/trymarz/yade-pgf) (branch `pgf-main`: YADE 2026.1.0 plus the per-particle `lambda` state and the `PgfCoupling` engine), at the commit PGF pins. Upstream YADE does not work. Background: [build.md → YADE DEM coupling](build.md#yade-dem-coupling).

**System packages.** YADE's build dependencies, the list `pgf pack` installs (`PGF_PACK_YADE_DEPS` in `utilities/pgf/pack.sh`):

```bash
sudo apt-get install cmake git g++ python3-dev python3-numpy python3-mpi4py python3-mpmath python3-matplotlib python3-ipython python3-pygraphviz python3-xlib python3-sphinx python3-tk libboost-all-dev libeigen3-dev libgts-dev libcgal-dev libvtk9-dev libsqlite3-dev libbz2-dev zlib1g-dev libopenblas-dev libsuitesparse-dev libmetis-dev coinor-libclp-dev libmpfr-dev libmpfrc++-dev libmpc-dev
```

On Debian 13, `libvtk9-dev` cannot be installed beside `python3-paraview` ([Pitfalls](#pitfalls)).

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

The run's output, kept in the case's `log.yade`, shows the solver's `DEM coupling: active` and the handshake on both sides, `pgfDemCoupler: handshake ok, protocol v2` and `PgfCoupling: handshake ok, protocol v2`; [build.md → Checking that the coupling is in](build.md#checking-that-the-coupling-is-in) lists the other checks.

## Container (no build on the server)

`pgf pack` builds an Apptainer image of one commit, with OpenFOAM, the pinned yade-pgf and PGF inside; the server then needs only Apptainer: [Apptainer images](containers.md).

## Pitfalls

- **One MPI.** YADE and the solver run in one `mpirun`, so YADE, `mpi4py` and OpenFOAM must use the same Open MPI. Build YADE against the system Open MPI that `openfoam2606-dev` uses, not a ThirdParty or conda one.
- **ParaView and `libvtk9-dev` (Debian 13).** `python3-paraview`, which `pgf view` needs, conflicts with `libvtk9-dev`, which the YADE build needs. The order that works: [build.md → ParaView and Yade's VTK](build.md#paraview-and-yades-vtk).
- **Memory for the YADE build.** Each `make` job needs roughly 3–4 GB; pick `-j` from the free memory, not the core count, or `cc1plus` is killed. Rerunning `make` resumes.
- **YADE off the pin.** After a `git pull` that moves `submodules/yade-pgf`, rerun the two `git submodule update` lines above (without `--init`) and rebuild YADE; `pgf doctor --yade` names the commits when they differ.
- **Single-interface hosts.** On a host whose only network is `lo`, or with docker or libvirt bridges, Open MPI can hang the coupled run; `pgf` then restricts it to loopback by itself (`PGF_MPI=auto`; [`pgf help config`](pgf-reference.md#pgf-help-config)).

## Related pages

- [README quick start](../README.md#quick-start)
- [The pgf tool](pgf.md), [Building](build.md), [Apptainer images](containers.md)
- [All documentation](README.md)
