#include <stdlib.h> 
#include <string.h>
#include <stdio.h>
#include <omp.h>

#include "../include/validation.h"

#define BFS_THREADS 2

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
        for (i = 0; i < board.rows_count; i++)
            if (i != x && board.grid[i * board.cols_count + y] == cell_value && block->solution[i * board.cols_count + y] == WHITE)
                return false;
        for (j = 0; j < board.cols_count; j++)
            if (j != y && board.grid[x * board.cols_count + j] == cell_value && block->solution[x * board.cols_count + j] == WHITE)
                return false;
    }
    return true;
}

// TODO: fix parallelization

int bfs_white_cells_parallel(Board board, BCB *block, bool *visited, int row, int col) {

    const int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int i, count = 0, visited_count = 0; // Counters for white cells and visited cells
    
    int board_size = board.rows_count * board.cols_count;
    int queue_x[board_size], queue_y[board_size];
    int front = 0, back = 0;

    // Enqueue the starting cell
    queue_x[back] = row;
    queue_y[back++] = col;
    visited[row * board.cols_count + col] = true;
    visited_count++;

    // Limit the number of threads to avoid synchronization overhead
    #pragma omp parallel num_threads(2) reduction(+:count) private(i)
    {
        // int tid = omp_get_thread_num();
        
        while (visited_count < board_size || front < back) { // While there are unvisited cells or the queue is not empty
            
            int cur_x = -1, cur_y = -1;

            // Dequeue a cell
            #pragma omp critical
            {
                if (front < back) { // Check if the queue is not empty
                    cur_x = queue_x[front];
                    cur_y = queue_y[front++];
                }
            }

            // Check if the cell is valid
            if (cur_x == -1 || cur_y == -1) continue;

            // if (DEBUG) printf("[%d] Dequeued cell: (%d, %d)\n", tid, cur_x, cur_y);

            count++; // Count the current white cell

            // Process all the adjacent cells
            for (i = 0; i < 4; i++) {
                int new_row = cur_x + directions[i][0];
                int new_col = cur_y + directions[i][1];
                int new_index = new_row * board.cols_count + new_col;

                bool row_in_bounds = new_row >= 0 && new_row < board.rows_count;
                bool col_in_bounds = new_col >= 0 && new_col < board.cols_count;

                if (row_in_bounds && col_in_bounds && !visited[new_index]) {

                    // Visit the new cell
                    #pragma omp critical
                    {
                        if (!visited[new_index]) { // Double-check inside critical section
                            visited[new_index] = true;
                            visited_count++; // Increment the total visited cells count
                            
                            // Enqueue the new white cell
                            if (block->solution[new_index] == WHITE) {
                                queue_x[back] = new_row;
                                queue_y[back++] = new_col;
                                // if (DEBUG) printf("[%d] Enqueued cell: (%d, %d)\n", tid, new_row, new_col);
                            }
                        }
                    }
                }
            }
        }
    }

    return count;
}

int bfs_white_cells(Board board, BCB *block, bool *visited, int row, int col) {

    const int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int i, count = 0; // Counter for white cells
    
    // Queues for BFS, one for x coordinates and one for y coordinates
    int max_size = board.rows_count * board.cols_count;
    int queue_x[max_size], queue_y[max_size];
    int front = 0, back = 0;

    // Enqueue the starting cell and visit it
    queue_x[back] = row;
    queue_y[back++] = col;
    visited[row * board.cols_count + col] = true;

    while (front < back) {
        
        // Dequeue a cell
        int cur_x = queue_x[front];
        int cur_y = queue_y[front++];

        // Increment the count of white cells
        count++;

        // Check all 4 directions
        for (i = 0; i < 4; i++) {
            int new_row = cur_x + directions[i][0];
            int new_col = cur_y + directions[i][1];
            int new_index = new_row * board.cols_count + new_col;

            if (new_row >= 0 && new_row < board.rows_count && new_col >= 0 && new_col < board.cols_count) {
                if (!visited[new_index] && block->solution[new_index] == WHITE) {

                    // Enqueue the new white cell and visit it
                    visited[new_index] = true;
                    queue_x[back] = new_row;
                    queue_y[back++] = new_col;
                }
            }
        }
    }

    return count;
}

bool check_hitori_conditions(Board board, BCB* block) {
    
    /*
        Hitori Rules:
            Rule 1: No unshaded number appears in a row or column more than once (✓)
            Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner (✓)
            Rule 3: When completed, all un-shaded (white) squares create a single continuous area

        The first two rules are already checked by the is_cell_state_valid function.
        The third rule is checked by the bfs_white_cells function.
    */

    int board_size = board.rows_count * board.cols_count;
    bool visited[board_size];
    memset(visited, false, board_size * sizeof(bool));

    // TODO: remove and count all cells instea
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

    // Check if the number of white cells is equal to the number of connected white cells (meaning a single continuous area)
    return bfs_white_cells(board, block, visited, row, col) == white_cells_count;
}

