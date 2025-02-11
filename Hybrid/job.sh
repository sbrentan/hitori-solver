#!/bin/bash
#PBS -l select=4:ncpus=4:mem=2gb
#PBS -l place=scatter
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

module load mpich-3.2

export OMP_NUM_THREADS=4

mpirun.actual -n 4 ./build/main.out input-20x20.txt