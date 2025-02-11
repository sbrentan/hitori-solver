#!/bin/bash
#PBS -l select=4:ncpus=2:mem=2gb
#PBS -l place=scatter
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

module load mpich-3.2
cd ./hitori-solver/MPI
mpirun.actual -n 8 ./build/main.out input-20x20.txt