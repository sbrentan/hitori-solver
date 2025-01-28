#include <stdlib.h> 
#include <string.h>
#include <mpi.h>

#include "../include/validation.h"

bool is_cell_state_valid(Board board, BCB* block, int x, int y, CellState cell_state) {

    // Rule 1: No unshaded number appears in a row or column more than once
    // Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner
    
    if (cell_state == BLACK) {
        if (x > 0 && block->solution[(x - 1) * board.cols_count + y] == BLACK) return false;
        if (x < board.rows_count - 1 && block->solution[(x + 1) * board.cols_count + y] == BLACK) return false;
        if (y > 0 && block->solution[x * board.cols_count + y - 1] == BLACK) return false;
        if (y < board.cols_count - 1 && block->solution[x * board.cols_count + y + 1] == BLACK) return false;
    } else if (cell_state == WHITE) {
        int i, j, cell_value = board.grid[x * board.cols_count + y];
        // TODO: optimize this (if rows=columns) or use a sum table
        for (i = 0; i < board.rows_count; i++)
            if (i != x && board.grid[i * board.cols_count + y] == cell_value && block->solution[i * board.cols_count + y] == WHITE)
                return false;
        for (j = 0; j < board.cols_count; j++)
            if (j != y && board.grid[x * board.cols_count + j] == cell_value && block->solution[x * board.cols_count + j] == WHITE)
                return false;
    }
    return true;
}

int dfs_white_cells(Board board, BCB *block, bool* visited, int row, int col) {
    if (row < 0 || row >= board.rows_count || col < 0 || col >= board.cols_count) return 0;
    if (visited[row * board.cols_count + col]) return 0;
    if (block->solution[row * board.cols_count + col] == BLACK) return 0;

    visited[row * board.cols_count + col] = true;

    int count = 1;
    count += dfs_white_cells(board, block, visited, row - 1, col);
    count += dfs_white_cells(board, block, visited, row + 1, col);
    count += dfs_white_cells(board, block, visited, row, col - 1);
    count += dfs_white_cells(board, block, visited, row, col + 1);
    return count;
}

bool all_white_cells_connected(Board board, BCB* block) {

    bool *visited = malloc((board.rows_count * board.cols_count) * sizeof(bool));
    memset(visited, false, board.rows_count * board.cols_count * sizeof(bool));

    // Count all the white cells, and find the first white cell
    int i, j;
    int row = -1, col = -1;
    int white_cells_count = 0;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (block->solution[i * board.cols_count + j] == WHITE) {
                // Count white cells
                white_cells_count++;

                // Find the first white cell
                if (row == -1 && col == -1) {
                    row = i;
                    col = j;
                }
            }
        }
    }

    // Rule 3: When completed, all un-shaded (white) squares create a single continuous area
    return dfs_white_cells(board, block, visited, row, col) == white_cells_count;
}

bool check_hitori_conditions(Board board, BCB* block, double* dfs_time, double* conditions_time) {
    
    // Rule 1: No unshaded number appears in a row or column more than once
    // Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner
    //  - Already checked in is_cell_state_valid while building the leaf

    // Rule 3: When completed, all un-shaded (white) squares create a single continuous area

    double start_dfs_time = MPI_Wtime();
    bool result = all_white_cells_connected(board, block);
    *dfs_time += MPI_Wtime() - start_dfs_time;
    if (!result) return false;

    return true;
}
