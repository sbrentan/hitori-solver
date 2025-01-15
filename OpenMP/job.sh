#!/bin/bash
#PBS -l select=2:ncpus=4:mem=2gb
#PBS -l walltime=0:20:00
#PBS -q short_cpuQ
#PBS -o ./output/
#PBS -e ./output/

cd ./hitori-solver/OpenMP
./build/main.out 4 input-5x5.txt