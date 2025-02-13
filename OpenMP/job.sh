#!/bin/bash
#PBS -l select=1:ncpus=16:mem=4gb
#PBS -l place=pack:excl
#PBS -l walltime=0:02:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

export OMP_NUM_THREADS=16

./build/main.out test-30x30.txt