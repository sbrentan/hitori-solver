#ifndef VALIDATION_H
#define VALIDATION_H

#include "common.h"

bool is_cell_state_valid(Board board, BCB* block, int x, int y, CellState cell_state);
bool bfs_white_cells_connected(Board board, BCB *block, int threads_available);

#endif