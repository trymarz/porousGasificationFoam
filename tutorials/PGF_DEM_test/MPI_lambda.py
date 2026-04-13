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



frame_number = 0

parallelYade=True 
numProcOF=2

#Lambda=1

O.periodic = False # if true, define O.cell.setBox
#O.cell.setBox(0.01000005, 0.0100005, 0.0100005)   #comment out if O.periodic = False
numspheres = 100
young = 5e6
density = 1000
NSTEPS = 5000#int(2e5)

O.materials.append(FrictMat(young=young, poisson=0.5, frictionAngle=radians(15), density=density, label='spheremat'))
O.materials.append(FrictMat(young=young * 100, poisson=0.5, frictionAngle=0, density=0, label='wallmat'))


"""
v0 = Vector3(-0.005, -0.005, -0.01)
v1 = Vector3( 0.005, -0.005, -0.01)
v2 = Vector3( 0.005,  0.005, -0.01)
v3 = Vector3(-0.005,  0.005, -0.01)

v4 = Vector3(-0.005, -0.005,  0.01)
v5 = Vector3( 0.005, -0.005,  0.01)
v6 = Vector3( 0.005,  0.005,  0.01)
v7 = Vector3(-0.005,  0.005,  0.01)


minval = 0

maxvalX = 0.01
maxvalY = 0.01
maxvalZ = 0.02

v0 = Vector3(minval, minval, minval)
v1 = Vector3(maxvalX, minval, minval)
v2 = Vector3(maxvalX, maxvalY, minval)
v3 = Vector3(minval, maxvalY, minval)

v4 = Vector3(minval, minval, maxvalZ)
v5 = Vector3(maxvalX, minval, maxvalZ)
v6 = Vector3(maxvalX, maxvalY, maxvalZ)
v7 = Vector3(minval, maxvalY, maxvalZ)



yminus = 0.25 * (v0 + v1 + v4 + v5)
yplus  = 0.25 * (v2 + v3 + v6 + v7)


mn, mx = Vector3(5e-5, 5e-5, 5e-5), Vector3(0.0095, 0.0095, 0.0195) # sphere cloud

sp = pack.SpherePack()
sp.makeCloud(mn, mx, rMean=0.00075, rRelFuzz=0.4, num=numspheres)
O.bodies.append([sphere(center, rad, material='spheremat') for center, rad in sp])
O.bodies.append(box(center=yminus, extents=(maxvalX/2, 0, maxvalZ/2), fixed=True))
O.bodies.append(box(center=yplus, extents=(maxvalX/2, 0, maxvalZ/2), fixed=True))

"""



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
min_elongation_percent = 50             # freeze threshold (compression)
max_elongation_percent = 50               # break threshold (expansion)

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



springsLambda = { sid: 1.0 for sid in springsList }  # per-spring lambda storage

# broadcast springsLambda to all MPI ranks
if mp.rank == 0:
    springsLambda_serial = [(sid, springsLambda[sid]) for sid in springsLambda]
else:
    springsLambda_serial = None

springsLambda_serial = mp.comm.bcast(springsLambda_serial, root=0)

springsLambda = { sid: lam for (sid, lam) in springsLambda_serial }

if mp.rank != 0:
    print(f"[rank {mp.rank}] received springsLambda for {len(springsLambda)} springs")



def apply_spring_forces():
    """ 
    Each rank computes forces for springs which endpoints it can access locally,
    then master rank gathers all forces and applies them consistently.
    """
    if O.iter == 2:
    	print("[CHECK] apply_spring_forces is running on rank", mp.rank)
	    
    global springsList, broken_springs, springsLambda

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

        
        # PER-SPRING DYNAMIC LAMBDA UPDATE    (***THIS IS AN IMPORTANT PART***)
        # ------------------------------------------------------------

        lambdaDot_i = bi.state.lambdaDot
        lambdaDot_j = bj.state.lambdaDot

        lambdaDot_avg = -(0.5 * (lambdaDot_i + lambdaDot_j))  # should not be negative ! negative is just to test here
        lambdaDot_avg *= 1e4    # scaling factor to increase the impact for now just for test


        lambda_prev = springsLambda[spring_id]
        lambda_new = lambda_prev + O.dt * lambdaDot_avg

        # clamp for stability
        lambda_new = max(0.1, min(lambda_new, 2.0))

        springsLambda[spring_id] = lambda_new

        # new target rest length
        target_L = L0 * lambda_new

        # Hooke force
        lambda_eff = L_current - target_L
        Fvec = spring_k * lambda_eff * direction
        
        if spring_id == 0:
            print(
                f"[DEBUG] iter={O.iter}  "
                f"λ_prev={lambda_prev:.6f}  "
                f"λ_new={lambda_new:.6f}  "
                f"Δλ={lambda_new - lambda_prev:.2e}  "
            )

  
        # store local force contributions
        local_forces.append((i,  Fvec[0],  Fvec[1],  Fvec[2]))
        local_forces.append((j, -Fvec[0], -Fvec[1], -Fvec[2]))

    # ---------------- MPI GATHER ----------------
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
            del springsLambda[sid]



"""
    for spring_id, (i, j, L0) in springsList.items():

        # skip missing bodies (may not exist on this rank)
        if (i >= len(O.bodies)) or (j >= len(O.bodies)):
            continue
        if (O.bodies[i] is None) or (O.bodies[j] is None):
            continue

        # rank may NOT own both particles, but can still compute the force
        pi = O.bodies[i].state.pos
        pj = O.bodies[j].state.pos

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
            broken_springs.append((
                spring_id, i, j, O.time,
                L0, L_current, L_current-L0, elong_pct
            ))
            to_remove.append(spring_id)
            continue

        # NORMAL FORCE
           
        #target_L = L0 * Lambda  # if lambda is constant and pre defined 
        
        #perpring lambda calculation based on lambdaDot of the two spheres connected to the spring
        lambdaDot_i = O.bodies[i].state.lambdaDot
        lambdaDot_j = O.bodies[j].state.lambdaDot

        lambda_spring = 1 - (0.5 * (lambdaDot_i + lambdaDot_j))   # average

        # desired rest length corrected by local lambda_spring
        target_L = L0 * lambda_spring

        # Hookean 
        lambda_eff = L_current - target_L
        Fvec = spring_k * lambda_eff * direction

        # 
        # IMPORTANT: collect LOCAL forces, but DO NOT apply them yet.
        local_forces.append((i,  Fvec[0],  Fvec[1],  Fvec[2]))
        local_forces.append((j, -Fvec[0], -Fvec[1], -Fvec[2]))

    
    #  gather force from al lranks
    # ------------------------------
    all_forces = mp.comm.gather(local_forces, root=0)

    if mp.rank == 0:
        merged = defaultdict(lambda: Vector3(0,0,0))
        for flist in all_forces:
            for sid, fx, fy, fz in flist:
                merged[sid] += Vector3(fx, fy, fz)

        keys = list(merged.keys())
        vals = [merged[k] for k in keys]
    else:
        keys, vals = None, None

    # BROADCAST FINAL FORCES
    keys = mp.comm.bcast(keys, root=0)
    vals = mp.comm.bcast(vals, root=0)

    
    #  EACH RANK APPLIES ONLY LOCAL FORCES
    # ------------------------------
    for sid, force in zip(keys, vals):
        if sid < len(O.bodies) and O.bodies[sid] is not None:
            O.forces.addF(sid, force)

    
    # REMOVE BROKEN SPRINGS
    # ------------------------------
    for sid in to_remove:
        if sid in springsList:
            del springsList[sid]
"""

# -------------------


fluidCoupling = FoamCoupling()
fluidCoupling.couplingModeParallel = parallelYade
fluidCoupling.isGaussianInterp = True


sphereIDs = [b.id for b in O.bodies if type(b.shape) == Sphere]


fluidCoupling.SetOpenFoamSolver("porousGasificationFoam", numProcOF)




# tell the coupling engine which IDs to couple
fluidCoupling.setIdList(sphereIDs)
fluidCoupling.setNumParticles(len(sphereIDs))



def printStep():
	print("step = ", O.iter)


def savePos():
    export.text(f"spheres/spheres_{O.iter:05d}.txt")
    
export.text("spheres/spheres_0.txt")
#export.VTKExporter("spheres/vtk-0").exportSpheres()



# Export springs as VTK ( not VTKRecorder )
# ---------------------
def export_springs():
    global frame_number
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

    # ----------------------------------------------------
    # gather all points from all ranks to ( master rank)
    # ----------------------------------------------------
    all_points = mp.comm.gather(local_points, root=0)

    # ----------------------------------------------------
    # 3) master rank writes a VTK file
    # ----------------------------------------------------
    if mp.rank == 0:

    
        pts = []
        for sub in all_points:
            pts.extend(sub)

        num_lines = len(pts) // 2

        file_path = f"springs/springs_{frame_number:04d}.vtk"
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

            # spring type ( nothing meaningful yet ) 
            f.write(f"\nCELL_DATA {num_lines}\n")
            f.write("SCALARS springType int 1\nLOOKUP_TABLE default\n")
            for _ in range(num_lines):
                f.write("0\n")

        print(f"[VTK] Exported {num_lines} springs -> {file_path}")
        frame_number += 1




"""
if mp.rank == 0:
    print("[YADE] FoamCoupling type:", type(fluidCoupling))
    print("[YADE] Has inCommProcs?", hasattr(fluidCoupling, "inCommProcs"))
    print("[YADE] dir(fluidCoupling):", dir(fluidCoupling))
"""    
    

def gatherHydroFT():
    local = []
    ids = fluidCoupling.getIdList()

    for i in ids:
        if i < len(O.bodies) and O.bodies[i] is not None:
            
            f = O.forces.f(i)
            t = O.forces.t(i)

            # NEW version: read lambdaDot from YADE state :D it works!
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

        # Print first 10
        for k in sorted(merged)[:10]:
            Fx, Fy, Fz, Tx, Ty, Tz, lam = merged[k]
            print(f"[hydro+lambdaDot] id {k}: F=({Fx:.3e},{Fy:.3e},{Fz:.3e}) "
                  f"T=({Tx:.3e},{Ty:.3e},{Tz:.3e}) lambdaDot={lam:.7e}")


def printlambdaDotNew():      
	for b in O.bodies:
	    if isinstance(b.shape, utils.Sphere):
	    	print(f"particle ID={b.id}  lambdaDot={b.state.lambdaDot}")

	    	#if b: print(f"particle ID=" {b.id}, " lambdaDot="{b.state.lambdaDot},)
    
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
        GlobalStiffnessTimeStepper(timestepSafetyCoefficient=0.7, defaultDt=5e-6, timeStepUpdateInterval=200, parallelMode=True, label="ts"),
        fluidCoupling,  #to be called after timestepper
        NewtonIntegrator(damping=0.99, label='newton', gravity=(0, 0.0, 0)),
        
        PyRunner(command='apply_spring_forces()', iterPeriod=1, firstIterRun=1), 	
        PyRunner(command="export_springs()", iterPeriod=10), # i works!
        #PyRunner(command='printlambdaDotNew()', iterPeriod=1), #it works! prints a list of sphereID and lambdaDot 
        #VTKRecorder(fileName='spheres/vtk-', recorders=['spheres'], parallelMode=True, virtPeriod=1e-3),
        VTKRecorder(fileName='spheres/vtk-', recorders=['spheres'], parallelMode=True, iterPeriod=10),

        PyRunner(iterPeriod=1, command='gatherHydroFT()'), # it works! prints a list of hydrodynamic forces + lambdaDot sent from OF
        #PyRunner(command='savePos()', iterPeriod=5000) 
       
         
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



