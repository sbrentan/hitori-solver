#!/bin/bash
#PBS -l select=1:ncpus=1:mem=2gb
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ

module load mpich-3.2
cd ./hitori-solver/MPI
mpirun.actual -n 1 ./hitori-solver-mpi.out