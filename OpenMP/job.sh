#!/bin/bash
#PBS -l select=1:ncpus=64:mem=4gb
#PBS -l place=pack:excl
#PBS -l walltime=0:02:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

export OMP_NUM_THREADS=64

./build/main.out test4-20x20.txt