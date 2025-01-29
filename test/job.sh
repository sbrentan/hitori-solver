#!/bin/bash
#PBS -l select=1:ncpus=8:mem=2gb
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

export OMP_NUM_THREADS=8

./build/main.out input-5x5.txt