#!/bin/bash
#SBATCH -N 32
#SBATCH -C cpu
#SBATCH -q regular
#SBATCH -J 4096cpu
#SBATCH -t 00:05:00

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
srun -n 4096 -c 2 --cpu_bind=cores ./warpx.2d.MPI.OMP.DP.PDP.OPMD.EB inputs.2d_4096cpu

