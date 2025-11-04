#!/bin/bash
#SBATCH -N 512
#SBATCH -C cpu
#SBATCH -q regular
#SBATCH -J 65536cpu
#SBATCH -t 00:05:00

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
srun -n 65536 -c 2 --cpu_bind=cores ./warpx.2d.MPI.OMP.DP.PDP.OPMD.EB inputs.2d_65536cpu

