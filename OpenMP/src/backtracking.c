#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#include "../include/backtracking.h"
#include "../include/validation.h"
#include "../include/utils.h"

bool build_leaf(Board board, BCB* block, int uk_x, int uk_y, int **unknown_index, int **unknown_index_length, int *total_processes_in_solution_space, int *solutions_to_skip) {

    /* if (!block->solution_space_unknowns[0] || !block->solution_space_unknowns[15] || !block->solution_space_unknowns[16]) {
        printf("[Build leaf] Error in solution space unknowns\n");
    } */
    
    int i, board_y_index;
    while (uk_x < board.rows_count && uk_y >= (*unknown_index_length)[uk_x]) {
        uk_x++;
        uk_y = 0;
    }

    if (uk_x == board.rows_count) {
        if ((*total_processes_in_solution_space) > 1) {
            (*solutions_to_skip)--;
            if ((*solutions_to_skip) == -1) 
                (*solutions_to_skip) = (*total_processes_in_solution_space) - 1;
            else {
                return false;
            }
        }
        return true;
    }

    
    board_y_index = (*unknown_index)[uk_x * board.cols_count + uk_y];

    CellState cell_state = block->solution[uk_x * board.cols_count + board_y_index];
    
    bool is_solution_space_unknown = block->solution_space_unknowns[uk_x * board.cols_count + uk_y];
    if (!is_solution_space_unknown) {
        if (cell_state == UNKNOWN)
            cell_state = WHITE;
    }
    
    if (cell_state == UNKNOWN){
        printf("[Build leaf] Cell is unknown\n");
        exit(-1);
    }

    for (i = 0; i < 2; i++) {
        if (is_cell_state_valid(board, block, uk_x, board_y_index, cell_state)) {
            block->solution[uk_x * board.cols_count + board_y_index] = cell_state;
            if (build_leaf(board, block, uk_x, uk_y + 1, unknown_index, unknown_index_length, total_processes_in_solution_space, solutions_to_skip))
                return true;
        }
        if (is_solution_space_unknown){
            break;
        }
        cell_state = BLACK;
    }
    if (!is_solution_space_unknown)
        block->solution[uk_x * board.cols_count + board_y_index] = UNKNOWN;
    return false;
}

bool next_leaf(Board board, BCB *block, int **unknown_index, int **unknown_index_length, int *total_processes_in_solution_space, int *solutions_to_skip) {
    int i, j, board_y_index;
    CellState cell_state;

    int num_thread = omp_get_thread_num();

    /* if (!block->solution_space_unknowns[0] || !block->solution_space_unknowns[15] || !block->solution_space_unknowns[16]) {
        printf("[Next leaf] Error in solution space unknowns\n");
    } */

    // find next white cell iterating unknowns from bottom
    for (i = board.rows_count - 1; i >= 0; i--) {
        for (j = (*unknown_index_length)[i] - 1; j >= 0; j--) {
            board_y_index = (*unknown_index)[i * board.cols_count + j];
            cell_state = block->solution[i * board.cols_count + board_y_index];

            if (block->solution_space_unknowns[i * board.cols_count + j]) {
                if (block->solution[i * board.cols_count + board_y_index] == UNKNOWN){
                    printf("[Next leaf] Solution space set unknown is unknown\n");
                    exit(-1);
                }
                return false;
            }

            if (cell_state == UNKNOWN) {
                printf("\n\n\n\n\n\n\n[%d] Cell is unknown\n", num_thread);
                // print_block("Block", block);
                printf("Unknown index: %d %d\n\n\n\n\n\n", i, board_y_index);
                printf("Vars: %d %d %d %d %d %d\n", i, j, board_y_index, cell_state, block->solution[i * board.cols_count + board_y_index], block->solution_space_unknowns[i * board.cols_count + j]);
                // exit(-1);
                // print solution
                print_block(board, "Block", block);
                // print unknowns
                fflush(stdout);
                return false;
            }

            if (cell_state == WHITE) {
                if (is_cell_state_valid(board, block, i, board_y_index, BLACK)) {
                    block->solution[i * board.cols_count + board_y_index] = BLACK;
                    if(build_leaf(board, block, i, j + 1, unknown_index, unknown_index_length, total_processes_in_solution_space, solutions_to_skip))
                        return true;
                }
            }

            block->solution[i * board.cols_count + board_y_index] = UNKNOWN;
        }
    }
    return false;
}

void init_solution_space(Board board, BCB* block, int solution_space_id, int **unknown_index) {
    
    block->solution = malloc(board.rows_count * board.cols_count * sizeof(CellState));
    block->solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool));

    memcpy(block->solution, board.solution, board.rows_count * board.cols_count * sizeof(CellState));
    memset(block->solution_space_unknowns, false, board.rows_count * board.cols_count * sizeof(bool));

    int i, j;
    int uk_idx, cell_choice, temp_solution_space_id = SOLUTION_SPACES - 1;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if ((*unknown_index)[i * board.rows_count + j] == -1)
                break;
            uk_idx = (*unknown_index)[i * board.rows_count + j];
            cell_choice = solution_space_id % 2;

            // Validate if cell_choice (black or white) here is valid
            //      If not valid, use fixed choice and do not decrease solution_space_id
            //      If neither are valid, set to unknown (then the loop will change it)
            if (!is_cell_state_valid(board, block, i, uk_idx, cell_choice)) {
                cell_choice = abs(cell_choice - 1);
                if (!is_cell_state_valid(board, block, i, uk_idx, cell_choice)) {
                    cell_choice = UNKNOWN;
                    continue;
                }
            }

            block->solution[i * board.cols_count + uk_idx] = cell_choice;
            block->solution_space_unknowns[i * board.cols_count + j] = true;

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

void compute_unknowns(Board board, int **unknown_index, int **unknown_index_length) {
    int i, j, temp_index = 0, total = 0;
    *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));
    for (i = 0; i < board.rows_count; i++) {
        temp_index = 0;
        for (j = 0; j < board.cols_count; j++) {
            int cell_index = i * board.cols_count + j;
            if (board.solution[cell_index] == UNKNOWN){
                (*unknown_index)[i * board.cols_count + temp_index] = j;
                temp_index++;
            }
        }
        (*unknown_index_length)[i] = temp_index;
        total += temp_index;
        if (temp_index < board.cols_count)
            (*unknown_index)[i * board.cols_count + temp_index] = -1;
    }
    printf("Total unknowns: %d\n", total);
    fflush(stdout);
}
