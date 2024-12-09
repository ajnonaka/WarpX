#!/bin/bash
#SBATCH -N 8
#SBATCH -C gpu
#SBATCH -G 32
#SBATCH -q debug
#SBATCH -J 32gpu
#SBATCH -t 00:05:00
#SBATCH -A mp111_g

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

#run the application:
#applications may perform better with --gpu-bind=none instead of --gpu-bind=single:1 
srun -n 32 -c 32 --cpu_bind=cores -G 32 --gpu-bind=single:1  ./warpx.2d.MPI.CUDA.DP.PDP.OPMD.EB inputs.2d_32gpu

