#! /bin/bash
blockMesh
#createPatch -overwrite | tee log_createPatch
cp -r 0.orig 0
setFields | tee log_setFields
decomposePar
mkdir spheres

mpirun --allow-run-as-root -n 2 yade MPI_lambda.py | tee log.txt
