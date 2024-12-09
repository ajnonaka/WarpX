#!/bin/bash
#SBATCH -N 1
#SBATCH -C cpu
#SBATCH -q debug
#SBATCH -J WarpX
#SBATCH -t 00:05:00

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
srun -n 64 -c 4 --cpu_bind=cores ./warpx.2d.MPI.OMP.DP.PDP.OPMD.EB inputs.2d_64cpu

