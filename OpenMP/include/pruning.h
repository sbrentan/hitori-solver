#ifndef PRUNING_H
#define PRUNING_H

#include "common.h"

Board uniqueness_rule(Board board);
Board set_white(Board board);
Board set_black(Board board);
Board sandwich_rules(Board board);
Board pair_isolation(Board board);
void compute_corner(Board board, int x, int y, CornerType corner_type, int **local_corner_solution);
Board corner_cases(Board board);
Board flanked_isolation(Board board);

#endif