#ifndef PRUNING_H
#define PRUNING_H

#include "common.h"

Board openmp_uniqueness_rule(Board board, int size);
Board openmp_set_white(Board board, int size);
Board openmp_set_black(Board board, int size);
Board openmp_sandwich_rules(Board board, int size);
Board openmp_pair_isolation(Board board, int size);
void compute_corner(Board board, int x, int y, CornerType corner_type, int **local_corner_solution);
Board openmp_corner_cases(Board board, int size);
Board openmp_flanked_isolation(Board board, int size);

#endif