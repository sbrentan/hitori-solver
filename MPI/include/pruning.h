#ifndef PRUNING_H
#define PRUNING_H

#include "common.h"

Board mpi_uniqueness_rule(Board board, int rank, int size);
Board mpi_set_white(Board board, int rank, int size);
Board mpi_set_black(Board board, int rank, int size);
Board mpi_sandwich_rules(Board board, int rank, int size);
Board mpi_pair_isolation(Board board, int rank, int size);
void compute_corner(Board board, int x, int y, CornerType corner_type, int **local_corner_solution);
Board mpi_corner_cases(Board board, int rank, int size);
Board mpi_flanked_isolation(Board board, int rank, int size);

#endif