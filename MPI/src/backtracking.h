#ifndef BACKTRACKING_H
#define BACKTRACKING_H

#include <mpi.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "libs/types.h"
#include "libs/utils.h"

/* ------------------ SOLUTION VALIDATION ------------------ */

bool is_cell_state_valid(Board* board, BCB* block, int x, int y, CellState cell_state) {
    if (cell_state == BLACK) {
        if (x > 0 && block->solution[(x - 1) * board->cols_count + y] == BLACK) return false;
        if (x < board->rows_count - 1 && block->solution[(x + 1) * board->cols_count + y] == BLACK) return false;
        if (y > 0 && block->solution[x * board->cols_count + y - 1] == BLACK) return false;
        if (y < board->cols_count - 1 && block->solution[x * board->cols_count + y + 1] == BLACK) return false;
    } else if (cell_state == WHITE) {
        int i, j, cell_value = board->grid[x * board->cols_count + y];
        // TODO: optimize this (if rows=columns) or use a sum table
        for (i = 0; i < board->rows_count; i++)
            if (i != x && board->grid[i * board->cols_count + y] == cell_value && block->solution[i * board->cols_count + y] == WHITE)
                return false;
        for (j = 0; j < board->cols_count; j++)
            if (j != y && board->grid[x * board->cols_count + j] == cell_value && block->solution[x * board->cols_count + j] == WHITE)
                return false;
    }
    return true;
} 

int dfs_white_cells(Board* board, BCB *block, bool* visited, int row, int col) {
    if (row < 0 || row >= board->rows_count || col < 0 || col >= board->cols_count) return 0;
    if (visited[row * board->cols_count + col]) return 0;
    if (block->solution[row * board->cols_count + col] == BLACK) return 0;

    visited[row * board->cols_count + col] = true;

    int count = 1;
    count += dfs_white_cells(board, block, visited, row - 1, col);
    count += dfs_white_cells(board, block, visited, row + 1, col);
    count += dfs_white_cells(board, block, visited, row, col - 1);
    count += dfs_white_cells(board, block, visited, row, col + 1);
    return count;
}

bool all_white_cells_connected(Board* board, BCB* block) {

    bool *visited = (bool*) malloc((board->rows_count * board->cols_count) * sizeof(bool));
    memset(visited, false, board->rows_count * board->cols_count * sizeof(bool));

    // Count all the white cells, and find the first white cell
    int i, j;
    int row = -1, col = -1;
    int white_cells_count = 0;
    for (i = 0; i < board->rows_count; i++) {
        for (j = 0; j < board->cols_count; j++) {
            if (block->solution[i * board->cols_count + j] == WHITE) {
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

bool check_hitori_conditions(Board* board, BCB* block) {
    
    // Rule 1: No unshaded number appears in a row or column more than once
    // Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner

    int i, j, k;
    for (i = 0; i < board->rows_count; i++) {
        for (j = 0; j < board->cols_count; j++) {

            if (block->solution[i * board->cols_count + j] == UNKNOWN) return false;

            if (block->solution[i * board->cols_count + j] == WHITE) {
                for (k = 0; k < board->rows_count; k++) {
                    if (k != i && block->solution[k * board->cols_count + j] == WHITE && board->grid[i * board->cols_count + j] == board->grid[k * board->cols_count + j]) return false;
                }

                for (k = 0; k < board->cols_count; k++) {
                    if (k != j && block->solution[i * board->cols_count + k] == WHITE && board->grid[i * board->cols_count + j] == board->grid[i * board->cols_count + k]) return false;
                }
            }

            if (block->solution[i * board->cols_count + j] == BLACK) {
                if (i > 0 && block->solution[(i - 1) * board->cols_count + j] == BLACK) return false;
                if (i < board->rows_count - 1 && block->solution[(i + 1) * board->cols_count + j] == BLACK) return false;
                if (j > 0 && block->solution[i * board->cols_count + j - 1] == BLACK) return false;
                if (j < board->cols_count - 1 && block->solution[i * board->cols_count + j + 1] == BLACK) return false;
            }
        }
    }

    if (!all_white_cells_connected(board, block)) return false;

    return true;
}

/* ------------------ SOLUTION BACKTRACKING ------------------ */

bool build_leaf(Board* board, BCB* block, int uk_x, int uk_y, int *unknown_index, int *unknown_index_length, int *solutions_to_skip, int *total_processes_in_solution_space, int rank) {

    /* if (!block->solution_space_unknowns[0] || !block->solution_space_unknowns[15] || !block->solution_space_unknowns[16]) {
        printf("[Build leaf] Error in solution space unknowns\n");
    } */
    
    int i, board_y_index;
    while (uk_x < board->rows_count && uk_y >= unknown_index_length[uk_x]) {
        uk_x++;
        uk_y = 0;
    }

    if (uk_x == board->rows_count) {
        if (*total_processes_in_solution_space > 1) {
            (*solutions_to_skip)--;
            if ((*solutions_to_skip) == -1) 
                (*solutions_to_skip) = *total_processes_in_solution_space - 1;
            else {
                printf("[Build leaf][%d] Skipping shared solution space %d %d\n", rank, *solutions_to_skip, *total_processes_in_solution_space);
                return false;
            }
            printf("[Build leaf][%d] Testing shared solution space %d %d\n", rank, *solutions_to_skip, *total_processes_in_solution_space);
        }
        return true;
    }

    
    board_y_index = unknown_index[uk_x * board->cols_count + uk_y];

    CellState cell_state = block->solution[uk_x * board->cols_count + board_y_index];
    
    bool is_solution_space_unknown = block->solution_space_unknowns[uk_x * board->cols_count + uk_y];
    if (!is_solution_space_unknown) {
        if (cell_state == UNKNOWN)
            cell_state = WHITE;
    } else {
        printf("[Build leaf][%d] Reached solution space unknown\n", rank);
    }
    
    if (cell_state == UNKNOWN){
        printf("[Build leaf] Cell is unknown\n");
        exit(-1);
    }

    for (i = 0; i < 2; i++) {
        if (is_cell_state_valid(board, block, uk_x, board_y_index, cell_state)) {
            block->solution[uk_x * board->cols_count + board_y_index] = cell_state;
            if (build_leaf(board, block, uk_x, uk_y + 1, unknown_index, unknown_index_length, solutions_to_skip, total_processes_in_solution_space, rank))
                return true;
        }
        if (is_solution_space_unknown){
            printf("[Build leaf][%d] Skipping solution space unknown\n", rank);
            break;
        }
        cell_state = BLACK;
    }
    if (!is_solution_space_unknown)
        block->solution[uk_x * board->cols_count + board_y_index] = UNKNOWN;
    return false;
}

bool next_leaf(Board* board, BCB *block, int *unknown_index, int *unknown_index_length, int* solutions_to_skip, int *total_processes_in_solution_space, int rank) {
    int i, j, board_y_index;
    CellState cell_state;

    /* if (!block->solution_space_unknowns[0] || !block->solution_space_unknowns[15] || !block->solution_space_unknowns[16]) {
        printf("[Next leaf] Error in solution space unknowns\n");
    } */

    // find next white cell iterating unknowns from bottom
    for (i = board->rows_count - 1; i >= 0; i--) {
        for (j = unknown_index_length[i] - 1; j >= 0; j--) {
            board_y_index = unknown_index[i * board->cols_count + j];
            cell_state = block->solution[i * board->cols_count + board_y_index];

            if (block->solution_space_unknowns[i * board->cols_count + j]) {
                if (block->solution[i * board->cols_count + board_y_index] == UNKNOWN){
                    printf("[Next leaf] Solution space set unknown is unknown\n");
                    exit(-1);
                }
                printf("[Next leaf][%d] Reached end of solution space\n", rank);
                return false;
            }

            if (cell_state == UNKNOWN) {
                printf("\n\n\n\n\n\n\nCell is unknown\n");
                //print_block("Block", block, board);
                printf("Unknown index: %d %d\n\n\n\n\n\n", i, board_y_index);
                // exit(-1);
                return false;
            }

            if (cell_state == WHITE) {
                if (is_cell_state_valid(board, block, i, board_y_index, BLACK)) {
                    block->solution[i * board->cols_count + board_y_index] = BLACK;
                    if(build_leaf(board, block, i, j + 1, unknown_index, unknown_index_length, solutions_to_skip, total_processes_in_solution_space, rank))
                        return true;
                }
            }

            block->solution[i * board->cols_count + board_y_index] = UNKNOWN;
        }
    }
    return false;
}

void init_solution_space(Board* board, BCB* block, int solution_space_id, int *unknown_index) {
    
    block->solution = (CellState*) malloc(board->rows_count * board->cols_count * sizeof(CellState));
    block->solution_space_unknowns = (bool*) malloc(board->rows_count * board->cols_count * sizeof(bool));

    memcpy(block->solution, board->solution, board->rows_count * board->cols_count * sizeof(CellState));
    memset(block->solution_space_unknowns, false, board->rows_count * board->cols_count * sizeof(bool));

    int i, j;
    CellState cell_choice;
    int uk_idx, temp_solution_space_id = SOLUTION_SPACES - 1;
    for (i = 0; i < board->rows_count; i++) {
        for (j = 0; j < board->cols_count; j++) {
            if (unknown_index[i * board->rows_count + j] == -1)
                break;
            uk_idx = unknown_index[i * board->rows_count + j];
            cell_choice = (CellState) (solution_space_id % 2);

            // Validate if cell_choice (black or white) here is valid
            //      If not valid, use fixed choice and do not decrease solution_space_id
            //      If neither are valid, set to unknown (then the loop will change it)
            if (!is_cell_state_valid(board, block, i, uk_idx, cell_choice)) {
                cell_choice = (CellState) abs(cell_choice - 1);
                if (!is_cell_state_valid(board, block, i, uk_idx, cell_choice)) {
                    cell_choice = UNKNOWN;
                    continue;
                }
            }

            block->solution[i * board->cols_count + uk_idx] = cell_choice;
            block->solution_space_unknowns[i * board->cols_count + j] = true;

            if (solution_space_id > 0)
                solution_space_id = solution_space_id / 2;
            
            if (temp_solution_space_id > 0)
                temp_solution_space_id = temp_solution_space_id / 2;
            
            if (temp_solution_space_id == 0)
                break;
        }

        if (temp_solution_space_id == 0)
            break;
    }
}

void block_to_buffer(Board* board, BCB* block, int **buffer) {
    int i;
    printf("a\n");
    // *buffer = (int *) malloc((board->rows_count * board->cols_count * 2) * sizeof(int));
    memcpy(*buffer, block->solution, board->rows_count * board->cols_count * sizeof(CellState));
    printf("b\n");
    for (i = 0; i < board->rows_count * board->cols_count; i++)
        (*buffer)[board->rows_count * board->cols_count + i] = block->solution_space_unknowns[i] ? 1 : 0;
    printf("c\n");
}

bool buffer_to_block(Board* board, int *buffer, BCB *block) {

    int i;
    block->solution = (CellState*) malloc(board->rows_count * board->cols_count * sizeof(CellState));
    block->solution_space_unknowns = (bool*) malloc(board->rows_count * board->cols_count * sizeof(bool));
    
    // if (buffer[0] == UNKNOWN) {
    //     printf("[ERROR] Process %d received unknown buffer\n", rank);
    //     return false;
    // }
    
    memcpy(block->solution, buffer, board->rows_count * board->cols_count * sizeof(CellState));
    for (i = 0; i < board->rows_count * board->cols_count; i++) {
        block->solution_space_unknowns[i] = buffer[board->rows_count * board->cols_count + i] == 1;
    }

    return true;
}

void compute_unknowns(Board* board, int **unknown_index, int **unknown_index_length) {
    int i, j, temp_index = 0, total = 0;
    *unknown_index = (int *) malloc(board->rows_count * board->cols_count * sizeof(int));
    *unknown_index_length = (int *) malloc(board->rows_count * sizeof(int));
    for (i = 0; i < board->rows_count; i++) {
        temp_index = 0;
        for (j = 0; j < board->cols_count; j++) {
            int cell_index = i * board->cols_count + j;
            if (board->solution[cell_index] == UNKNOWN){
                (*unknown_index)[i * board->cols_count + temp_index] = j;
                temp_index++;
            }
        }
        (*unknown_index_length)[i] = temp_index;
        total += temp_index;
        if (temp_index < board->cols_count)
            (*unknown_index)[i * board->cols_count + temp_index] = -1;
    }
}

#endif