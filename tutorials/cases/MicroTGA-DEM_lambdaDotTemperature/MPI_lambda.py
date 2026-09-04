import os
from yade import mpy as mp
from yade import export 
import math
from collections import defaultdict
import numpy as np
from mpi4py import MPI
from yade import pack



comm = mp.comm_slave



"""
print("\n=== Available communicators in mpy:  ")
for a in dir(mp):
    if "comm" in a.lower():
        print(a, "=>", getattr(mp, a))
#print("======================================\n")
 """
#print("YADE: current working directory:", os.getcwd())

writeInterval = 0.1   # seconds — Yade VTK write interval (separate from OF writeInterval)


spring_frame_number = 0

#timeRatio = 5000  # itterations to write vtk files

yadeDtFixed = 5e-6 #5e-6
O.dynDt = False
O.dt    = yadeDtFixed


timeRatio = int(round(writeInterval / yadeDtFixed))
timeRatio = max(1, timeRatio)


if mp.rank == 0:
    print("[EXPORT TIMEs] yadeDtFixed=", yadeDtFixed, " writeInterval=", writeInterval, " iterPeriod=", timeRatio)



parallelYade=True 
numProcOF=2

#Lambda=1

O.periodic = False # if true, define O.cell.setBox
#O.cell.setBox(0.01000005, 0.0100005, 0.0100005)   #comment out if O.periodic = False
numspheres = 100
young = 25e6 #5e6
density = 1200
NSTEPS = int(os.environ.get('YADE_NSTEPS', 2000000))

# VTKRecorder cadence for spheres: timeRatio (writeInterval-derived) never
# fires within this fixture's short NSTEPS, so pick a period scaled to the
# actual run length instead.
vtkSphereIterPeriod = max(1, NSTEPS // 10)

O.materials.append(FrictMat(young=young, poisson=0.5, frictionAngle=radians(15), density=density, label='spheremat'))
O.materials.append(FrictMat(young=young * 100, poisson=0.5, frictionAngle=0, density=0, label='wallmat'))



# size of the domain  containing the spheres
maxvalX = 0.01
maxvalY = 0.01
maxvalZ = 0.02

#Put the domain center at (0,0,0)
hx, hy, hz = maxvalX/2., maxvalY/2., maxvalZ/2.

v0 = Vector3(-hx, -hy, -hz)
v1 = Vector3( hx, -hy, -hz)
v2 = Vector3( hx,  hy, -hz)
v3 = Vector3(-hx,  hy, -hz)

v4 = Vector3(-hx, -hy,  hz)
v5 = Vector3( hx, -hy,  hz)
v6 = Vector3( hx,  hy,  hz)
v7 = Vector3(-hx,  hy,  hz)

yminus = 0.25*(v0 + v1 + v4 + v5)   # (0, -hy, 0)
yplus  = 0.25*(v2 + v3 + v6 + v7)   # (0, +hy, 0)



margin = 5e-5
mn = Vector3(-hx + margin, -hy + margin, -hz + margin)
mx = Vector3( hx - margin,  hy - margin,  hz - margin)

sp = pack.SpherePack()
sp.makeCloud(mn, mx, rMean=0.00075, rRelFuzz=0, num=numspheres)

O.bodies.append([sphere(c, r, material='spheremat') for c, r in sp])

O.bodies.append(box(center=yminus, extents=(hx, 0, hz), fixed=True))
O.bodies.append(box(center=yplus,  extents=(hx, 0, hz), fixed=True))




# spring creation 
#----------------
spring_damping = 1.0
mass = 0.000268                          # same as wood sample
spring_k = (spring_damping**2) / (4 * mass)
min_elongation_percent = 80             # freeze threshold (compression)
max_elongation_percent = 100               # break threshold (expansion)

print(f"[SPRINGS] spring_k = {spring_k:.6e}")

n_neighbors = 6
springsList = {}                 # springID : (i,j,L0)
broken_springs = []
existing_pairs = set()
spring_id_counter = 0


# collect sphere IDs
sphereIDs = [b.id for b in O.bodies if isinstance(b.shape, Sphere)]
positions  = [O.bodies[i].state.pos for i in sphereIDs]

# KD-tree for nearest neighboring spheres
from scipy.spatial import cKDTree
tree = cKDTree(positions)

for idx, sid in enumerate(sphereIDs):
    dists, idxs = tree.query(positions[idx], k=n_neighbors+1)  # +1 for itself

    for k in range(1, len(idxs)):      # skip itself
        sid2 = sphereIDs[idxs[k]]
        pair = tuple(sorted((sid, sid2)))
        if pair in existing_pairs:
            continue

        existing_pairs.add(pair)
        L0 = dists[k]

        springsList[spring_id_counter] = (pair[0], pair[1], L0)
        spring_id_counter += 1

print(f"[SPRINGS] Created {len(springsList)} springs.")

# save initial springs list 

os.makedirs("springs", exist_ok=True)
with open("springs/springsList.txt", "w") as f:
    f.write("springID sphere1 sphere2 length\n")
    for sid, (i,j,L0) in springsList.items():
        f.write(f"{sid} {i} {j} {L0:.6e}\n")



# send springsList to all MPI RANKS ( broadcast) 
# ----------------------------------------

if mp.rank == 0:
    springs_serial = [(sid, data[0], data[1], data[2]) for sid, data in springsList.items()]
else:
    springs_serial = None

# broadcast to all ranks
springs_serial = mp.comm.bcast(springs_serial, root=0)

# reconstruct springsList on ALL ranks
springsList = { sid : (i, j, L0) for (sid, i, j, L0) in springs_serial }

if mp.rank != 0:
    print(f"[rank {mp.rank}] received {len(springsList)} springs")



# No Python-side lambda storage. Yade integrates lambda itself, per body, in
# NewtonIntegrator (State::lambda_ += lambdaDot*dt every DEM step) from the
# lambdaDot OpenFOAM sends, and sends the integrated value back to OpenFOAM.
# A shadow dict here would be a second, independently-integrated copy of the
# same quantity, free to drift from both.


def apply_spring_forces():
    """
    Each rank computes forces for springs which endpoints it can access locally,
    then master rank gathers all forces and applies them consistently.
    """

    global springsList, broken_springs

    local_forces = []   # (sphereID, Fx, Fy, Fz)
    to_remove = []
    
    for spring_id, (i, j, L0) in springsList.items():

        # skip missing bodies (may not exist on this rank)
        if (i >= len(O.bodies)) or (j >= len(O.bodies)):
            continue
        bi = O.bodies[i]
        bj = O.bodies[j]
        if bi is None or bj is None:
            continue

        # positions
        pi = bi.state.pos
        pj = bj.state.pos

        delta = pj - pi
        L_current = delta.norm()
        if L_current == 0:
            continue

        direction = delta.normalized()

        # FREEZE
        min_L = (min_elongation_percent/100) * L0
        if L_current <= min_L:
            continue

        # BREAK
        elong_pct = ((L_current - L0) / L0) * 100
        if elong_pct > max_elongation_percent:
            broken_springs.append((spring_id, i, j, O.time,
                                   L0, L_current, L_current - L0, elong_pct))
            to_remove.append(spring_id)
            continue


        # PER-SPRING LAMBDA                                         ***THIS IS AN IMPORTANT PART***
        # ------------------------------------------------------------
        # lambda is read, not integrated, here. Each body carries its own
        # integrated value in state.lambda_ (NewtonIntegrator advances it every
        # DEM step from the lambdaDot OpenFOAM sends); a spring just averages
        # its two endpoints to get a rest-length scaling.

        lambda_avg = 0.5 * (bi.state.lambda_ + bj.state.lambda_)

        # Local safety clamp on the force computation only -- it does not write
        # back into the integrated state, so a clamped spring does not corrupt
        # what OpenFOAM reads.
        lambda_avg = max(0.1, min(lambda_avg, 2.0))

        # new target rest length
        target_L = L0 * lambda_avg

        # Hooke force
        lambda_eff = L_current - target_L
        Fvec = spring_k * lambda_eff * direction

        """if spring_id == 0:
            print(
                f"[DEBUG] iter={O.iter}  "
                f"λ_avg={lambda_avg:.6f}  "
                f"λ_i={bi.state.lambda_:.6f}  "
                f"λ_j={bj.state.lambda_:.6f}  "
            ) """


        # store local force contributions
        local_forces.append((i,  Fvec[0],  Fvec[1],  Fvec[2]))
        local_forces.append((j, -Fvec[0], -Fvec[1], -Fvec[2]))

    #MPI GATHER
    all_forces = mp.comm.gather(local_forces, root=0)

    if mp.rank == 0:
        merged = defaultdict(lambda: Vector3(0, 0, 0))
        for flist in all_forces:
            for sid, fx, fy, fz in flist:
                merged[sid] += Vector3(fx, fy, fz)

        keys = list(merged.keys())
        vals = [merged[k] for k in keys]
    else:
        keys, vals = None, None

    # broadcast back
    keys = mp.comm.bcast(keys, root=0)
    vals = mp.comm.bcast(vals, root=0)

    # apply forces locally
    for sid, force in zip(keys, vals):
        if sid < len(O.bodies) and O.bodies[sid] is not None:
            O.forces.addF(sid, force)

    # remove broken springs
    for sid in to_remove:
        if sid in springsList:
            del springsList[sid]

#----------


fluidCoupling = FoamCoupling()
fluidCoupling.couplingModeParallel = parallelYade
fluidCoupling.isGaussianInterp = True


sphereIDs = [b.id for b in O.bodies if type(b.shape) == Sphere]


fluidCoupling.SetOpenFoamSolver("porousGasificationFoam", numProcOF)




# tell the coupling engine which IDs to couple
fluidCoupling.setIdList(sphereIDs)
fluidCoupling.setNumParticles(len(sphereIDs))





def printAndSaveDtInfo():
    yadeDt = O.dt
    foamDt = fluidCoupling.foamDeltaT
    ratio  = foamDt / yadeDt if yadeDt > 0 else float("inf")

    msg = (f"[Time Step INFO] iter={O.iter}  time={O.time:.6e}  "
           f"yadeDt={yadeDt:.6e}  foamDt={foamDt:.6e}  ratio={ratio:.6f}")

    # print once in terminal (rank 0 only)
    if mp.rank == 0:
        #print(msg)

        # append so all write points are preserved across iterations
        write_header = not os.path.exists("dtInfo.txt")
        with open("dtInfo.txt", "a") as f:
            if write_header:
                f.write("iter time yadeDt foamDt ratio\n")
            f.write(f"{O.iter} {O.time:.6e} {yadeDt:.6e} {foamDt:.6e} {ratio:.6f}\n")




def printStep():
	print("step = ", O.iter)


def savePos():
    export.text(f"spheres/spheres_{O.iter:05d}.txt")
    
export.text("spheres/spheres_0.txt")
#export.VTKExporter("spheres/vtk-0").exportSpheres()


# to list all vtk files in a single pvd file with time stamps. in paraview only open this pvd file not all vtk files.
# Spheres use Yade's native VTKRecorder (see O.engines) instead -- it writes
# its own .pvd, so only the hand-rolled springs exporter needs this here.

pvd_springs = []

def write_pvd(pvd_path, entries):
    with open(pvd_path, "w") as f:
        f.write('<?xml version="1.0"?>\n')
        f.write('<VTKFile type="Collection" version="0.1" byte_order="LittleEndian">\n')
        f.write('  <Collection>\n')
        for t, fname in entries:
            f.write(f'    <DataSet timestep="{t:.6f}" file="{fname}"/>\n')
        f.write('  </Collection>\n')
        f.write('</VTKFile>\n')


# Export springs as VTK ( not VTKRecorder )
# ---------------------
def export_springs():
    global spring_frame_number
    os.makedirs("springs", exist_ok=True)

    #Each rank takes a LOCAL list of endpoints
    # ---------
    local_points = []

    for spring_id, (i, j, L0) in springsList.items():
        # if bodies  not exist on this rank!
        if i >= len(O.bodies) or j >= len(O.bodies):
            continue
        bi = O.bodies[i]
        bj = O.bodies[j]
        if bi is None or bj is None:
            continue

        # local rank can read positions even if it does not own them
        local_points.append(bi.state.pos)
        local_points.append(bj.state.pos)


    # gather all points from all ranks to master
    # --------------------
    all_points = mp.comm.gather(local_points, root=0)


    # master rank writes a VTK file
    # ------------------------------------
    if mp.rank == 0:

    
        pts = []
        for sub in all_points:
            pts.extend(sub)

        num_lines = len(pts) // 2

        # this part works for classic vtk file paraview can not read them if they are listed in a pvt file.
        """file_path = f"springs/springs_{spring_frame_number:.1f}.vtk"
        with open(file_path, "w") as f:
            f.write("# vtk DataFile Version 3.0\nSpring network\nASCII\nDATASET POLYDATA\n")

            # points
            f.write(f"POINTS {len(pts)} float\n")
            for p in pts:
                f.write(f"{p[0]} {p[1]} {p[2]}\n")

            #lines
            f.write(f"\nLINES {num_lines} {num_lines*3}\n")
            idx = 0
            for k in range(num_lines):
                f.write(f"2 {idx} {idx+1}\n")
                idx += 2

            # spring type ( nothing specific for now, all the same time )
            f.write(f"\nCELL_DATA {num_lines}\n")
            f.write("SCALARS springType int 1\nLOOKUP_TABLE default\n")
            for _ in range(num_lines):
                f.write("0\n")

        print(f"[VTK] Exported {num_lines} springs -> {file_path}")"""

        file_path = f"springs/springs_{spring_frame_number:.1f}.vtp"
        with open(file_path, "w") as f:
            f.write('<?xml version="1.0"?>\n')
            f.write('<VTKFile type="PolyData" version="0.1" byte_order="LittleEndian">\n')
            f.write('  <PolyData>\n')
            f.write(f'    <Piece NumberOfPoints="{len(pts)}" NumberOfLines="{num_lines}">\n')

            # Points
            f.write('      <Points>\n')
            f.write('        <DataArray type="Float32" NumberOfComponents="3" format="ascii">\n')
            for p in pts:
                f.write(f'          {p[0]} {p[1]} {p[2]}\n')
            f.write('        </DataArray>\n')
            f.write('      </Points>\n')

            # Lines (connectivity + offsets)
            f.write('      <Lines>\n')

            f.write('        <DataArray type="Int32" Name="connectivity" format="ascii">\n')
            # each spring uses two consecutive points (spheres cente r):
            for k in range(num_lines):
                f.write(f'          {2*k} {2*k+1}\n')
            f.write('        </DataArray>\n')

            f.write('        <DataArray type="Int32" Name="offsets" format="ascii">\n')
            #  cumulative counts
            for k in range(num_lines):
                f.write(f'          {2*(k+1)}\n')
            f.write('        </DataArray>\n')

            f.write('      </Lines>\n')

            # CellData: springType ( it is optional for now ! all springs are the same type for now
            f.write('      <CellData Scalars="springType">\n')
            f.write('        <DataArray type="Int32" Name="springType" format="ascii">\n')
            for _ in range(num_lines):
                f.write('          0\n')
            f.write('        </DataArray>\n')
            f.write('      </CellData>\n')

            f.write('    </Piece>\n')
            f.write('  </PolyData>\n')
            f.write('</VTKFile>\n')



        global pvd_springs
        vtk_name = os.path.basename(file_path)
        pvd_springs.append((spring_frame_number, vtk_name))
        write_pvd("springs/springs.pvd", pvd_springs)

        spring_frame_number += float(writeInterval)


def gatherHydroFT():
    local = []
    ids = fluidCoupling.getIdList()

    for i in ids:
        if i < len(O.bodies) and O.bodies[i] is not None:
            
            f = O.forces.f(i)
            t = O.forces.t(i)

            # read lambdaDot from YADE state :D it works!
            lam = O.bodies[i].state.lambdaDot

            local.append((i, (f[0], f[1], f[2],
                               t[0], t[1], t[2],
                               lam)))

    all_data = mp.comm.gather(local, root=0)

    if mp.rank == 0:
        merged = {}
        for lst in all_data:
            for i, vals in lst:
                merged[i] = vals

        # Print first 5
        for k in sorted(merged)[:10]:
            Fx, Fy, Fz, Tx, Ty, Tz, lam = merged[k]
            print(f"[Forces+lambdaDot (5 points only)] id {k}: F=({Fx:.3e},{Fy:.3e},{Fz:.3e}) "
                  f"T=({Tx:.3e},{Ty:.3e},{Tz:.3e}) lambdaDot={lam:.7e}")


#---------------
# ENGINES 
#---------------

    
O.engines = [
        ForceResetter(),
        InsertionSortCollider([Bo1_Sphere_Aabb(), Bo1_Box_Aabb()], label='collider', allowBiggerThanPeriod=True),
        InteractionLoop(
                [Ig2_Sphere_Sphere_ScGeom(), Ig2_Box_Sphere_ScGeom()], [Ip2_FrictMat_FrictMat_FrictPhys()], [Law2_ScGeom_FrictPhys_CundallStrack()],
                label='InteractionLoop'
        ),
        #GlobalStiffnessTimeStepper(timestepSafetyCoefficient=0.7, defaultDt=yadeDtFixed, timeStepUpdateInterval=20, parallelMode=True, label="ts"),
        fluidCoupling,  #to be called after timestepper
        NewtonIntegrator(damping=0.99, label='newton', gravity=(0, 0.0, 0)),
        
        PyRunner(command='apply_spring_forces()', iterPeriod=20, firstIterRun=1),
        PyRunner(command="export_springs()", iterPeriod=timeRatio, firstIterRun=1),
        VTKRecorder(fileName='spheres/vtk-', recorders=['spheres'], parallelMode=True, iterPeriod=vtkSphereIterPeriod),


        PyRunner(iterPeriod=timeRatio, command='gatherHydroFT()'), # it works! prints a list of hydrodynamic forces + lambdaDot sent from OF
        #PyRunner(command='savePos()', iterPeriod=5000) 

        PyRunner(command="printAndSaveDtInfo()", iterPeriod=timeRatio, firstIterRun=1)

        ]
        
#---------------       
        
collider.verletDist = 0.00075
mp.YADE_TIMING = False
mp.FLUID_COUPLING = True
mp.VERBOSE_OUTPUT = False
mp.USE_CPP_INTERS = False
mp.ERASE_REMOTE_MASTER = True
mp.REALLOC_FREQUENCY = 12
mp.fluidBodies = sphereIDs
mp.DOMAIN_DECOMPOSITION = True
mp.mpirun(NSTEPS,np=numProcOF)
mp.mprint("RUN FINISH")      

        

exit()



