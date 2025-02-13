#!/bin/bash
#PBS -l select=8:ncpus=2:mem=4gb
#PBS -l place=scatter:excl
#PBS -l walltime=0:02:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

module load mpich-3.2

mpirun.actual -n 16 ./build/main.out test-30x30.txt