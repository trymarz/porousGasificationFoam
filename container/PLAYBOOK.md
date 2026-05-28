# Apptainer image playbook — porousGasificationFoam + YADE

**For:** HPC operators building and deploying the image.  
**Last updated:** 2026-05-28

---

## What this image provides

A self-contained Apptainer image that runs porousGasificationFoam + YADE DEM on HPC
clusters using the **bind-host-MPI** model: the cluster's own MPI is mounted over
`/opt/openmpi` at runtime so the solver communicates across nodes via the site fabric.

The image bundles OpenFOAM v2406 (ESI), YADE (Foam-Yade fork), and PGF itself — all
built against a single in-container OpenMPI whose version you choose at build time to
match the cluster.

---

## Components and constraints

### YADE fork

- URL: `https://github.com/zhiwar-ep/Foam-Yade` — a full YADE fork (not just the
  coupling), forked from `gitlab.com/yade-dev/trunk`.
- Last tested commit: `9c8b4c7` (2026-05-22).
- Build system: CMake ≥ 3.18, C++17. Flags needed:
  `-DENABLE_MPI=ON -DENABLE_FOAMCOUPLING=ON`.
- **Ubuntu 22.04 is YADE-dev's own CI platform** (`.gitlab-ci.yml` tests exclusively
  on `registry.gitlab.com/yade-dev/docker-yade:ubuntu22.04`). This is why "Ubuntu
  22.04" appears as a constraint: it is YADE's supported environment, not PGF's or
  OpenFOAM's. Modernizing past it means getting ahead of YADE's CI.
- OpenFOAM coupling code lives at `pkg/openfoam/coupling/FoamYade/` (contains
  `FoamYade.{C,H}`, `meshtree/meshTree.{C,H}`, `commYade/yadeComm.{C,H}`, each with
  their own wmake `Make/` dirs).
- The coupling's own Make/options already uses the correct two-token split
  (`-L$(FOAM_LIBBIN)/$(FOAM_MPI)` then `-lPstream` on separate lines). The bug below
  existed only on the PGF side.

### gcc-9

The gcc-9 pin is **not a hard YADE requirement**. Ubuntu 22.04's default is gcc-11
and the docker-yade image uses the distro compiler with no override. gcc-9 is most
likely a workaround the user found when PGF + YADE first crashed under a newer
compiler. It is **not** a blocker for containerization — the 22.04 environment (with
whatever gcc ships in the docker-yade base) is what the container uses, and that is
already what YADE-dev tests against.

### OpenFOAM

Target: **OpenFOAM-v2406** (ESI). Binary packages available for Ubuntu 22.04.  
The Pstream library in the binary package links the distro's system OpenMPI. The
def file rebuilds Pstream from the installed source (`$WM_PROJECT_DIR/src/Pstream`)
against `/opt/openmpi` so the MPI ABI is consistent across all three consumers.

---

## The one concrete bug found in this repo

`porousGasificationMedia/DEM/Make/options` line 18 had a malformed link flag:

```make
# WRONG: expands to a -L search path into a non-existent dir named "lPstream"
-L$(FOAM_LIBBIN)/$(FOAM_MPI)/lPstream
```

Should be two separate tokens:

```make
-L$(FOAM_LIBBIN)/$(FOAM_MPI) \
-lPstream
```

**This was fixed in commit `fix: correct malformed -lPstream link flag in DEM/Make/options`
on this branch.** The def file also applies the same fix defensively via `sed` during
the build in case an older checkout is used.

---

## Why the bind-MPI model and what makes it fail

The bind model: host `mpirun` launches processes; the container provides the
application binaries; the host MPI's shared libraries are mounted into the container
at runtime over `/opt/openmpi`, overriding the in-container MPI.

**Hard requirement:** OpenMPI does NOT guarantee cross-minor ABI. The container OpenMPI
must be built at the same **major.minor** as the host's OpenMPI. A version mismatch is
the single most likely cause of a failed bind attempt (symptoms: unresolved symbols,
every rank silently reporting rank 0, single-process runs that should span nodes).

**Three MPI consumers that must all agree on one ABI:**
1. OpenFOAM Pstream (the parallel communication library)
2. YADE's C++ MPI (built into the YADE binary via `-DENABLE_MPI=ON`)
3. `mpi4py` (used in `tutorials/cases/DEM_UsInterp_solidU/MPI_lambda.py` as
   `from mpi4py import MPI`)

All three are built against `/opt/openmpi` in the def file. At runtime, the host bind
replaces `/opt/openmpi` with the cluster's ABI-compatible OpenMPI.

**Common failure modes (other than version mismatch):**
- Missing bind paths: the host MPI's plugins, PMIx libs, and interconnect user-space
  libs (UCX, libfabric, `libibverbs`, `/etc/libibverbs.d`) must all be visible inside.
- Transport fallback: if UCX/verbs aren't bound, OpenMPI may refuse to fall back to
  TCP silently. Diagnose with `OMPI_MCA_btl_base_verbose=100`.
- `--allow-run-as-root` is already present in the DEM tutorial's `mpirun` call
  (`tutorials/cases/DEM_UsInterp_solidU/Allrun`). This matters in-container if the
  image runs as root (Apptainer default in some HPC configs).

---

## Required before building: host OpenMPI version

You must know the cluster's **exact OpenMPI version `X.Y.Z`** and whether UCX/PMIx are
compiled in. This determines whether the `OMPI_VERSION=4.1.6` default needs changing.

Run this on the cluster (after loading the OpenMPI module) before building:

```bash
module avail 2>&1 | grep -i openmpi
module load <the-openmpi-module>
ompi_info | grep -iE 'Open MPI:|Open RTE:|PMIx'
ompi_info | grep -iE 'ucx|btl|fabric'
which mpirun; mpirun --version
```

Record the exact `X.Y.Z`. Then rebuild the image with that version:

```bash
apptainer build --build-arg OMPI_VERSION=X.Y.Z \
    container/pgf-yade.sif container/pgf-yade.def
```

If UCX or PMIx are absent on the cluster, the `--with-ucx` / `--with-pmix` configure
flags in Step 2 of the def may warn (not fail) during the image build. They can be
dropped with no functional penalty since the in-container MPI is only a placeholder
for the host ABI match.

---

## How to build the image

```bash
# From the repository root (required — %files copies "." into the image):
cd /path/to/porousGasificationFoam

# Default (OpenMPI 4.1.6):
apptainer build container/pgf-yade.sif container/pgf-yade.def

# With a specific OpenMPI version:
apptainer build \
    --build-arg OMPI_VERSION=4.1.8 \
    container/pgf-yade.sif container/pgf-yade.def
```

Requires: Apptainer ≥ 1.0, internet access (fetches OpenMPI tarball, ESI apt repo,
Foam-Yade git clone), and `--fakeroot` or root (for `%post` with apt-get). On most
HPC systems: `apptainer build --fakeroot ...`.

Expected build time: 45–90 min on a modern workstation (YADE CMake build is the
bottleneck).

---

## Verification ladder

Run these on the cluster in order. Stop at the first failure — each rung isolates
exactly one layer.

| # | Command | Confirms |
|---|---------|----------|
| 1 | `apptainer exec pgf-yade.sif mpirun -n 2 hostname` | in-container OpenMPI works |
| 2 | `mpirun -n 2 apptainer exec --bind HOST_MPI:/opt/openmpi pgf-yade.sif hostname` | host mpirun + bind + ABI match |
| 3 | Same as 2 but with `python3 -c "from mpi4py import MPI; print(MPI.COMM_WORLD.Get_rank())"` | mpi4py ABI correct |
| 4 | OpenFOAM `-parallel` on a tiny case, 2 ranks | Pstream works in parallel |
| 5 | `tutorials/cases/DEM_UsInterp_solidU` coupled DEM run, 2 ranks | full coupled path |

Replace `HOST_MPI` with the path to the cluster's OpenMPI install prefix (the directory
that contains `bin/mpirun` and `lib/libmpi.so`). Example:
`/opt/openmpi/4.1.6` or wherever `module load` puts it.

Diagnose rung 2 failures with:
```bash
OMPI_MCA_btl_base_verbose=100 mpirun -n 2 apptainer exec \
    --bind HOST_MPI:/opt/openmpi pgf-yade.sif hostname
```

---

## Architecture decisions in the def file

| Decision | Why |
|----------|-----|
| Base: `docker-yade:ubuntu22.04` | Pre-installs all YADE dependencies (boost, CGAL, VTK, Eigen, Qt) at exactly the versions YADE-dev tests. Avoids the 10-library guesswork of a clean Ubuntu base. |
| OpenFOAM binary package, then rebuild Pstream | ESI binary package is fast and well-maintained. We only need to rebuild the Pstream shared lib (not all of OpenFOAM) to get the right MPI ABI. |
| Rebuild YADE from source despite it being in docker-yade | The base's YADE links the distro OpenMPI. We need all three consumers to link `/opt/openmpi`, so YADE must be rebuilt. The base still provides the dependencies; only the YADE binaries are replaced. |
| `WM_PROJECT_USER_DIR=/opt/openfoam-user` | If left as the default `$HOME/OpenFOAM/$USER-v2406`, built binaries are placed in a path that changes with the user/home at runtime, making them invisible. A fixed path ensures the solver is always findable. |
| OpenMPI as `OMPI_VERSION` build arg | Keeps a single def file that can target any cluster by rebuilding with a different arg, without editing the file. |

---

## Modernization (Ubuntu 24.04) — optional, separate effort

If wanted after the 22.04 image works:
1. Switch base to `registry.gitlab.com/yade-dev/docker-yade:ubuntu24.04` (it exists
   in YADE CI but is not the primary tested platform).
2. YADE's CMake already has a gcc-13 workaround (`-Wno-error=array-bounds=`).
3. OpenFOAM-v2406 builds on gcc-12/13.
4. The unknown is whether `ENABLE_FOAMCOUPLING=ON` works on 24.04 — YADE-dev's own
   foam CI jobs only test on 22.04.

Do NOT block on this. The 22.04 image solves the stated problem regardless.
