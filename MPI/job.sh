#!/bin/bash
#PBS -l select=2:ncpus=1:mem=2gb
#PBS -l place=scatter
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

module load mpich-3.2

mpirun.actual -n 2 ./build/main.out test-25x25.txt