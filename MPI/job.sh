#!/bin/bash
#PBS -l select=4:ncpus=4:mem=4gb
#PBS -l place=scatter:excl
#PBS -l walltime=0:02:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

module load mpich-3.2

mpirun.actual -n 8 ./build/main.out input-20x20.txt
