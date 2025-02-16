#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/backtracking.h"
#include "../include/validation.h"

bool build_leaf(Board board, BCB* block, int uk_x, int uk_y, int **unknown_index, int **unknown_index_length, int *total_processes_in_solution_space, int *solutions_to_skip) {

    /*
        This function is responsible for building the initial leaves of the solution space tree.
    */
    
    /*
        Parameters:
            board: the board to be solved
            block: the BCB to analyze
            uk_x: index of the unknown row
            uk_y: index of the unknown column
            unknown_index: matrix with the indexes of the unknown cells
            unknown_index_length: vector containing the number of unknown cells in each row
            total_processes_in_solution_space: number of processes working in the solution space
            solutions_to_skip: number of solutions to skip in the current solution space to avoid duplicates
    */

    /*
        Increment the indexes uk_x and uk_y. When uk_y is greater than cols, reset it.
    */

    int i, board_y_index;
    while (uk_x < board.rows_count && uk_y >= (*unknown_index_length)[uk_x]) {
        uk_x++;
        uk_y = 0;
    }

    /*
        If uk_x is greater than the number of rows, the leaf is built.
        The nuber of solutions to skip is properly decremented.
    */

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

    
    /*
        Convert the uk_y index to the board_y_index
    */

    board_y_index = (*unknown_index)[uk_x * board.cols_count + uk_y];

    /*
        Get the cell state with the board_y_index
    */

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

    /*
        Try to set the cell state to white and black, continuing the iteration
    */

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

    /*
        This function is responsible for finding the next leaf in the solution space tree.
    */

    /*
        Parameters:
            board: the board to be solved
            block: the BCB to analyze
            unknown_index: matrix with the indexes of the unknown cells
            unknown_index_length: vector containing the number of unknown cells in each row
            total_processes_in_solution_space: number of processes working in the solution space
            solutions_to_skip: number of solutions to skip in the current solution space to avoid duplicates
    */

    /*
        Find the next white cell iterating unknowns from bottom. Change it to black and try to build the new solution.
    */

    int i, j, board_y_index;
    CellState cell_state;
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
                printf("[ERROR] Unexpected cell state: unknown\n");
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

    /*
        This function is responsible for initializing the solution spaces.
    */

    /*
        Parameters:
            board: the board to be solved
            block: the BCB to initialize
            solution_space_id: the id of the solution space
            unknown_index: matrix with the indexes of the unknown cells
    */

    /*
        Intialize the block, copying the pruned solution and setting the unknowns to false.
    */
    
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

            /*
                Validate if cell_choice (black or white) here is valid:
                    If not valid, use the other choice
                    If neither are valid, set to unknown (then the loop will change it later)
            */

            if (!is_cell_state_valid(board, block, i, uk_idx, cell_choice)) {
                cell_choice = abs(cell_choice - 1);
                if (!is_cell_state_valid(board, block, i, uk_idx, cell_choice)) {
                    cell_choice = UNKNOWN;
                    printf("[Error] Solution space invalid at cell id %d\n", i * board.cols_count + uk_idx);
                    continue;
                }
            }

            /*
                If the cell_choice is valid, update the solution the block and the solution space unknowns
            */

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

    /*
        This function is responsible for computing the unknown cells indexes.
    */

    /*
        Parameters:
            board: the board to be solved
            unknown_index: matrix with the indexes of the unknown cells to be computed
            unknown_index_length: vector containing the number of unknown cells in each row to be computed
    */

    /*
        Allocate memory for the unknown_index and unknown_index_length
    */

    *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));


    /*
        Iterate over the board. For each row, store the indexes of the unknown cells in the unknown_index matrix.
        Store the number of unknown cells in each row in the unknown_index_length vector. 
    */

    int i, j, temp_index = 0, total = 0;
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
}
