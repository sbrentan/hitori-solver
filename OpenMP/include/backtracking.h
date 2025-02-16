#ifndef BACKTRACKING_H
#define BACKTRACKING_H

#include "common.h"

bool build_leaf(Board board, BCB* block, int uk_x, int uk_y, int **unknown_index, int **unknown_index_length, int *total_processes_in_solution_space, int *solutions_to_skip);
bool next_leaf(Board board, BCB *block, int **unknown_index, int **unknown_index_length, int *total_processes_in_solution_space, int *solutions_to_skip);
void init_solution_space(Board board, BCB* block, int solution_space_id, int **unknown_index);
void compute_unknowns(Board board, int **unknown_index, int **unknown_index_length);

#endif