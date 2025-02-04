#include <stdlib.h> 
#include <string.h>
#include <stdio.h>
#include <omp.h>

#include "../include/validation.h"

#define BFS_THREADS 2  // TODO: update

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

bool bfs_white_cells_connected(Board board, BCB *block, int threads_available) {
    const int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int i, visited_count = 0; // Counters for visited cells
    int board_size = board.rows_count * board.cols_count;

    // Keeps track of the pid that visited the cell
    int visited[board_size];
    memset(visited, 0, board_size * sizeof(int));

    const int starting_position_one_thread[2] = {0, 0};
    const int starting_position_two_threads[2][2] = {{0, 0}, {board.rows_count - 1, board.cols_count - 1}};
    const int starting_position_three_threads[3][2] = {{0, 0}, {board.rows_count / 2, board.cols_count - 1}, {board.rows_count - 1, 0}};
    const int starting_position_four_threads[4][2] = {{0, 0}, {0, board.cols_count - 1}, {board.rows_count - 1, 0}, {board.rows_count - 1, board.cols_count - 1}};

    int starting_positions[4][2];
    switch (threads_available) {
        case 1:
            starting_positions[0][0] = starting_position_one_thread[0];
            starting_positions[0][1] = starting_position_one_thread[1];
            break;
        case 2:
            for (i = 0; i < 2; i++) {
                starting_positions[i][0] = starting_position_two_threads[i][0];
                starting_positions[i][1] = starting_position_two_threads[i][1];
            }
            break;
        case 3:
            for (i = 0; i < 3; i++) {
                starting_positions[i][0] = starting_position_three_threads[i][0];
                starting_positions[i][1] = starting_position_three_threads[i][1];
            }
            break;
        case 4:
            for (i = 0; i < 4; i++) {
                starting_positions[i][0] = starting_position_four_threads[i][0];
                starting_positions[i][1] = starting_position_four_threads[i][1];
            }
            break;
        default:
            printf("[ERROR] Invalid number of threads\n");
            return -1;
    }

    bool connections[threads_available][threads_available];
    memset(connections, false, threads_available * threads_available * sizeof(bool));

    // set visited for starting positions
    for (i = 0; i < threads_available; i++)
        visited[starting_positions[i][0] * board.cols_count + starting_positions[i][1]] = i + 1;
    visited_count += threads_available;
    
    // omp_lock_t locks[board_size];
    // for (i = 0; i < board_size; i++)
    //     omp_init_lock(&locks[i]);

    omp_lock_t lock;
    omp_init_lock(&lock);

    #pragma omp parallel num_threads(threads_available) reduction(+:visited_count) private(i)
    {
        int tid = omp_get_thread_num();
        connections[tid][tid] = true;
        int row = starting_positions[tid][0];
        int col = starting_positions[tid][1];
        int queue_x[board_size], queue_y[board_size];
        int front = 0, back = 0;
        bool row_in_bounds, col_in_bounds, is_visited;
        int new_row, new_col, new_index, cur_x, cur_y;
        

        // Enqueue the starting cell
        queue_x[back] = row;
        queue_y[back++] = col;
        
        // print board and cols count

        while (front < back) { // While there are unvisited cells or the queue is not empty

            cur_x = -1, cur_y = -1;

            // Dequeue a cell
            cur_x = queue_x[front];
            cur_y = queue_y[front++];

            // Process all the adjacent cells
            for (i = 0; i < 4; i++) {
                new_row = cur_x + directions[i][0];
                new_col = cur_y + directions[i][1];

                row_in_bounds = new_row >= 0 && new_row < board.rows_count;
                col_in_bounds = new_col >= 0 && new_col < board.cols_count;

                if (row_in_bounds && col_in_bounds) {
                    new_index = new_row * board.cols_count + new_col;

                    if (visited[new_index]) {
                        if (!connections[tid][visited[new_index] - 1]) {
                            connections[tid][visited[new_index] - 1] = true;
                            connections[visited[new_index] - 1][tid] = true;
                        }
                    } else {
                        // Visit the new cell
                        // TODO: change to lock?

                        is_visited = false;
                        omp_set_lock(&lock);
                        if (!visited[new_index]) {
                            visited[new_index] = tid + 1;
                            visited_count++; // Increment the total visited cells count
                            is_visited = true;
                        }
                        omp_unset_lock(&lock);
                        
                        if (is_visited) {
                            // Enqueue the new white cell
                            if (block->solution[new_index] == WHITE) {
                                queue_x[back] = new_row;
                                queue_y[back++] = new_col;
                            }
                        }
                    }
                }
            }
        }
    }
    omp_destroy_lock(&lock);

    // for (i = 0; i < board_size; i++)
    //     omp_destroy_lock(&locks[i]);
    
    if (visited_count != board_size) return false;

    bool visited_threads[threads_available];
    memset(visited_threads, false, threads_available * sizeof(bool));
    int thread_queue[threads_available];
    int front = 0, back = 0;

    thread_queue[back++] = 0;
    visited_threads[0] = true;
    int threads_count = 1;

    int cur_thread;
    while (front < back) {
        cur_thread = thread_queue[front++];
        for (i = 0; i < threads_available; i++) {
            if (connections[cur_thread][i] && !visited_threads[i]) {
                visited_threads[i] = true;
                thread_queue[back++] = i;
                threads_count++;
            }
        }
    }

    return threads_count == threads_available;
}
