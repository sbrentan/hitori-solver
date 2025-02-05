#!/bin/bash
#PBS -l select=1:ncpus=1:mem=2gb
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

export OMP_NESTED=TRUE
export OMP_NUM_THREADS=1

./build/main.out input-20x20.txt