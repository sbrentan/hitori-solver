#ifndef VALIDATION_H
#define VALIDATION_H

#include "common.h"

bool is_cell_state_valid(Board board, BCB* block, int x, int y, CellState cell_state);
int dfs_white_cells(Board board, BCB *block, bool* visited, int row, int col);
bool check_hitori_conditions(Board board, BCB* block);

#endif