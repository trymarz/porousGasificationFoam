#! /bin/bash
blockMesh
#createPatch -overwrite | tee log_createPatch

cp -r 0.orig 0
setFields | tee log_setFields
decomposePar
mkdir spheres
# 6. Create a symbolic link to Yade Install
#ln -s /path/to/yade/install/bin/yade-exec yadeimport.py
#ln -s /home/pawel/Programy/Yade/Yade-2412/install/bin/yade-2025-05-28.git-e608e46 yadeimport.py

#In yade serial:
#python3 scriptMPI.py
#./yadeimport.py scriptMPI.py
#/home/pawel/Programy/Yade/Yade-2412/install/bin/yade-2025-05-28.git-e608e46 scriptMPI.py

#In yade parallel


#mpirun --allow-run-as-root -n 2 /home/zhiwar/yade-install/bin/yade-Unknown yade.py | tee log.txt

#For running on a single machine
mpirun --allow-run-as-root --mca oob_tcp_if_include lo -n 2 /home/zhiwar/yade-install/bin/yade-Unknown yade.py | tee log.txt
