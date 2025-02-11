#!/bin/bash
#PBS -l select=1:ncpus=8:mem=2gb
#PBS -l place=pack
#PBS -l walltime=0:00:20
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd $PBS_O_WORKDIR

export OMP_NUM_THREADS=8

./build/main.out input-20x20.txt