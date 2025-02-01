#!/bin/bash
#SBATCH -N 2
#SBATCH -C cpu
#SBATCH -q debug
#SBATCH -J 256cpu
#SBATCH -t 00:05:00

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
srun -n 256 -c 2 --cpu_bind=cores ./warpx.2d.MPI.OMP.DP.PDP.OPMD.EB inputs.2d_256cpu

