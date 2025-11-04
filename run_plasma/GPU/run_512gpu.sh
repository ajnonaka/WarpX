#!/bin/bash
#SBATCH -N 128
#SBATCH -C gpu
#SBATCH -G 512
#SBATCH -q regular
#SBATCH -J 512gpu
#SBATCH -t 00:05:00
#SBATCH -A mp111_g

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

# pin to closest NIC to GPU
export MPICH_OFI_NIC_POLICY=GPU

srun -n 512 -c 32 --cpu_bind=cores -G 512 --gpu-bind=none  ./warpx.2d.MPI.CUDA.DP.PDP.OPMD.EB inputs.2d_512gpu

