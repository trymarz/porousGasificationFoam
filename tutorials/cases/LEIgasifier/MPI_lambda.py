"""Yade DEM + porousGasificationFoam coupling script for LEIgasifier tutorial.

Spheres represent char/biomass particles in the active reaction zone
(oxidation + lower pyrolysis zone, z=0.20→0.42 m).  The OpenFOAM solver
computes lambdaDot for each sphere via the solid chemistry model; this script
applies the resulting radius shrinkage each step and removes particles that
fall below the minimum threshold.

Usage (1 Yade rank, 2 OpenFOAM ranks spawned internally):
    mpirun -n 1 yade MPI_lambda.py
"""
import os
from itertools import count

import numpy as np
import vtk
from yade import mpy as mp, pack, ymport
from yade.system import O

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
parallelYade       = False       # single Yade rank; OF handles its own parallelism
numProcOF          = 2          # OpenFOAM MPI ranks
SAVE_VTK_VIRT_PERIOD = 0.01    # VTK sphere output interval (virtual seconds)
nsteps             = int(2e6)

sphere_radius      = 0.004      # initial sphere radius (m) — 4 mm biomass pellets

# Active packing zone: full bed above the freeboard.
# Fresh moist wood fills z=0.20→1.00; particles placed just above the
# freeboard boundary up to the top of the narrow throat/combustion section.
# The upper bulk (z>0.42) is the wider shell and is handled by the
# background porous solid phase (Ywood/porosity in PGF); DEM tracks
# the moving particle layer in the constriction + combustion zone.
pack_box_lo = (-0.07, -0.006, 0.21)
pack_box_hi = ( 0.07,  0.006, 0.41)

# ---------------------------------------------------------------------------
# DEM boundary — import mesh faces as rigid walls
# ---------------------------------------------------------------------------
facets = ymport.blockMeshDict("system/blockMeshDict")
O.bodies.append(facets)

# ---------------------------------------------------------------------------
# Sphere packing — regular hexagonal close-pack inside the active zone
# ---------------------------------------------------------------------------
pred = pack.inAlignedBox(pack_box_lo, pack_box_hi)
O.bodies.append(
    pack.regularHexa(pred, radius=sphere_radius, gap=sphere_radius * 0.1)
)
sphereIDs = [b.id for b in O.bodies if type(b.shape) == Sphere]
os.makedirs("spheres", exist_ok=True)

# ---------------------------------------------------------------------------
# Fluid coupling
# ---------------------------------------------------------------------------
fluidCoupling = FoamCoupling()
fluidCoupling.couplingModeParallel = parallelYade
fluidCoupling.isGaussianInterp = True          # smooth Us field

fluidCoupling.SetOpenFoamSolver("porousGasificationFoam", numProcOF)
fluidCoupling.setIdList(sphereIDs)

# ---------------------------------------------------------------------------
# Sphere radius shrinkage — driven by OpenFOAM lambdaDot
# lambdaDot < 1 → particle shrinking (char consumption)
# ---------------------------------------------------------------------------
def changeRadius():
    bodies = [b for b in O.bodies if type(b.shape) == Sphere]
    for b in bodies:
        new_rad = b.shape.radius * b.state.lambdaDot
        if new_rad < 1e-4:          # erase particles smaller than 0.1 mm
            fluidCoupling.eraseId(b.id)
            mp.bodyErase(b.id)
        else:
            b.shape.radius = new_rad

# ---------------------------------------------------------------------------
# VTK sphere output
# ---------------------------------------------------------------------------
_vtk_counter = count(1)
_file_names_proc = []


def write_VTK_spheres():
    sphr      = [b for b in O.bodies if type(b.shape) == Sphere]
    centres   = [b.state.pos    for b in sphr]
    radii     = [b.shape.radius for b in sphr]
    velocities = [b.state.vel   for b in sphr]

    points = vtk.vtkPoints()
    for p in centres:
        points.InsertNextPoint(p)

    verts = vtk.vtkCellArray()
    for i in range(len(centres)):
        verts.InsertNextCell(1)
        verts.InsertCellPoint(i)

    polydata = vtk.vtkPolyData()
    polydata.SetPoints(points)
    polydata.SetVerts(verts)

    radi_arr = vtk.vtkFloatArray()
    radi_arr.SetName("radius")
    for r in radii:
        radi_arr.InsertNextValue(r)
    polydata.GetPointData().SetScalars(radi_arr)

    vel_arr = vtk.vtkFloatArray()
    vel_arr.SetNumberOfComponents(3)
    vel_arr.SetName("velocity")
    for v in velocities:
        vel_arr.InsertNextTuple(v)
    polydata.GetPointData().SetVectors(vel_arr)

    t_stamp  = SAVE_VTK_VIRT_PERIOD * next(_vtk_counter)
    vtp_name = f"spheres/spheres-rank{mp.rank}-{t_stamp:.4f}.vtp"

    writer = vtk.vtkXMLPolyDataWriter()
    writer.SetFileName(vtp_name)
    writer.SetInputData(polydata)
    writer.SetDataModeToAscii()
    writer.Write()
    _file_names_proc.append({"fileName": vtp_name, "timeStamp": t_stamp})

# ---------------------------------------------------------------------------
# Yade engine list
# ---------------------------------------------------------------------------
O.engines = [
    ForceResetter(),
    InsertionSortCollider(
        [Bo1_Sphere_Aabb(), Bo1_Facet_Aabb()],
        label="collider",
    ),
    InteractionLoop(
        [Ig2_Sphere_Sphere_ScGeom(), Ig2_Facet_Sphere_ScGeom()],
        [Ip2_FrictMat_FrictMat_FrictPhys()],
        [Law2_ScGeom_FrictPhys_CundallStrack()],
    ),
    GlobalStiffnessTimeStepper(
        timestepSafetyCoefficient=0.7,
        defaultDt=1e-5,
        timeStepUpdateInterval=50,
        parallelMode=True,
        label="ts",
    ),
    fluidCoupling,
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=0.2, label="newton"),
    PyRunner(
        command="changeRadius()",
        virtPeriod=0.1 * SAVE_VTK_VIRT_PERIOD,
        label="radiusChanger",
    ),
    PyRunner(
        command="write_VTK_spheres()",
        virtPeriod=SAVE_VTK_VIRT_PERIOD,
    ),
]

# ---------------------------------------------------------------------------
# MPI run
# ---------------------------------------------------------------------------
# Single Yade rank + 2 OF ranks.
# np=1 means only the Yade master runs — no Yade domain-decomposition workers.
# DOMAIN_DECOMPOSITION=False because splitting the Yade domain across multiple
# Yade ranks would cause each rank to call FoamCoupling::StartFoamSolver, but
# only the master holds the valid OpenFOAM inter-communicator after
# MPI_Comm_spawn → worker ranks crash with MPI_Comm_size on a null handle.
mp.FLUID_COUPLING       = True
mp.DOMAIN_DECOMPOSITION = False
mp.YADE_TIMING          = False
mp.VERBOSE_OUTPUT       = False
mp.USE_CPP_INTERS       = True
mp.ERASE_REMOTE_MASTER  = False
mp.REALLOC_FREQUENCY    = 0
mp.fluidBodies          = sphereIDs
mp.mpirun(nSteps=nsteps, np=1)

fluidCoupling.killMPI()
mp.mprint("LEIgasifier run finished")
exit()
