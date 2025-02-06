#!/bin/bash
#PBS -l select=1:ncpus=4:mem=2gb
#PBS -l walltime=0:02:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

export OMP_NUM_THREADS=4

./build/main.out test-25x25.txt