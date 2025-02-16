#!/bin/bash
#PBS -l select=8:ncpus=2:mem=4gb
#PBS -l place=scatter:excl
#PBS -l walltime=00:05:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

module load mpich-3.2

export OMP_NUM_THREADS=2

mpirun.actual -n 8 ./build/main.out test-26x26.txt