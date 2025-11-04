#!/bin/bash
#SBATCH -N 128
#SBATCH -C cpu
#SBATCH -q regular
#SBATCH -J 16384cpu
#SBATCH -t 00:05:00

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
srun -n 16384 -c 2 --cpu_bind=cores ./warpx.2d.MPI.OMP.DP.PDP.OPMD.EB inputs.2d_16384cpu

