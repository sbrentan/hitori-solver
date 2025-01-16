#ifndef PRUNING_H
#define PRUNING_H

#include "common.h"

Board openmp_uniqueness_rule(Board board);
Board openmp_set_white(Board board);
Board openmp_set_black(Board board);
Board openmp_sandwich_rules(Board board);
Board openmp_pair_isolation(Board board);
void compute_corner(Board board, int x, int y, CornerType corner_type, int **local_corner_solution);
Board openmp_corner_cases(Board board);
Board openmp_flanked_isolation(Board board);

#endif