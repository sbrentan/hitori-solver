#!/bin/bash
#PBS -l select=2:ncpus=8:mem=2gb
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ

module load mpich-3.2
cd ./hitori-solver/MPI
mpirun.actual -n 16 ./hitori-solver-mpi.out