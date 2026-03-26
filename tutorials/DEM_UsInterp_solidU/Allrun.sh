#!/bin/sh

cp -r 0_orig 0
blockMesh -dict system/blockMeshDict
setFields
> oo.foam
decomposePar
mpirun -np 4 porousGasificationFoam_DEM -parallel
#porousGasificationFoam
