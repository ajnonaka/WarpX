#!/bin/bash
#SBATCH -N 8
#SBATCH -C cpu
#SBATCH -q debug
#SBATCH -J 1024cpu
#SBATCH -t 00:05:00

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
srun -n 1024 -c 2 --cpu_bind=cores ./warpx.2d.MPI.OMP.DP.PDP.OPMD.EB inputs.2d_1024cpu

