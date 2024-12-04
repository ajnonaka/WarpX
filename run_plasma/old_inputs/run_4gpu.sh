#!/bin/bash
#SBATCH -N 1
#SBATCH -C gpu
#SBATCH -G 4
#SBATCH -q debug
#SBATCH -J WarpX_plasma
#SBATCH -t 00:05:00
#SBATCH -A mp111_g

#OpenMP settings:
export OMP_NUM_THREADS=1
export OMP_PLACES=threads
export OMP_PROC_BIND=spread

# pin to closest NIC to GPU
export MPICH_OFI_NIC_POLICY=GPU

srun -n 4 -c 32 --cpu_bind=cores -G 4 --gpu-bind=none ./warpx.2d.MPI.CUDA.DP.PDP.OPMD.EB inputs_plasma my_constants.scaling_fac=2
