#ifndef LIBS_BOARD_H
#define LIBS_BOARD_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <mpi.h>

#include "types.h"
#include "utils.h"

Board transpose(Board board) {

    /*
        Helper function to transpose a matrix.
    */

    Board Tboard;
    Tboard.rows_count = board.cols_count;
    Tboard.cols_count = board.rows_count;
    Tboard.grid = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    Tboard.solution = (CellState *) malloc(board.rows_count * board.cols_count * sizeof(CellState));

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            Tboard.grid[j * board.rows_count + i] = board.grid[i * board.cols_count + j];
            Tboard.solution[j * board.rows_count + i] = board.solution[i * board.cols_count + j];
        }
    }

    return Tboard;
}

Board deep_copy(Board board){
    Board copy = board;
    copy.grid = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    copy.solution = (CellState *) malloc(board.rows_count * board.cols_count * sizeof(CellState));

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            copy.grid[i * board.cols_count + j] = board.grid[i * board.cols_count + j];
            copy.solution[i * board.cols_count + j] = board.solution[i * board.cols_count + j];
        }
    }

    return copy;
}

bool is_board_equal(Board first_board, Board second_board, BoardType type) {

    /*
        Helper function to check if two boards are equal.
    */

    if (first_board.rows_count != second_board.rows_count || first_board.cols_count != second_board.cols_count) return false;

    int *first = (type == BOARD) ? first_board.grid : (int *)first_board.solution;
    int *second = (type == BOARD) ? second_board.grid : (int *)second_board.solution;

    int i, j;
    for (i = 0; i < first_board.rows_count; i++)
        for (j = 0; j < first_board.cols_count; j++)
            if (first[i * first_board.cols_count + j] != second[i * first_board.cols_count + j]) return false;

    return true;
}

Board combine_board_solutions(Board first_board, Board second_board, bool forced) {

    /*
        Combine the solutions of two boards to get the final solution.
    */

    Board final;
    final.grid = first_board.grid;
    final.rows_count = first_board.rows_count;
    final.cols_count = first_board.cols_count;
    final.solution = (CellState *) malloc(final.rows_count * final.cols_count * sizeof(CellState));
    memset(final.solution, UNKNOWN, final.rows_count * final.cols_count * sizeof(CellState));

    int rows = final.rows_count;
    int cols = final.cols_count;

    /*
        Combine the solutions by performing a pairwise comparison:
        1) If the values are the same, keep the value
        2) If the values are unknown and white, mark the cell as white
        3) If the values are different, mark the cell as black
    */

    int i, j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            if (first_board.solution[i * cols + j] == second_board.solution[i * cols + j]) final.solution[i * cols + j] = first_board.solution[i * cols + j];
            else if (!forced && first_board.solution[i * cols + j] == WHITE && second_board.solution[i * cols + j] == UNKNOWN) final.solution[i * cols + j] = WHITE;
            else if (!forced && first_board.solution[i * cols + j] == UNKNOWN && second_board.solution[i * cols + j] == WHITE) final.solution[i * cols + j] = WHITE;
            else if (!forced) final.solution[i * cols + j] = BLACK;
        }   
    }

    return final;
}

Board combine_partial_solutions(Board board, CellState *row_solution, CellState *col_solution, char *technique, bool forced, bool transpose_cols) {

    /*
        Combine the partial solutions from the rows and columns to get the final solution.
    */

    Board row_board = board;
    row_board.solution = (CellState *) row_solution;

    Board col_board = board;
    col_board.solution = (CellState *) col_solution;

    if (transpose_cols)
        col_board = transpose(col_board);

    if (DEBUG) {
        printf("\n# --- %s --- #\n", technique);
        print_board("[Rows]", row_board, SOLUTION);
        print_board("[Cols]", col_board, SOLUTION);
    }

    return combine_board_solutions(row_board, col_board, forced);
}

Board mpi_compute_and_share(Board board, CellState *row_solution, CellState *col_solution, bool forced, char *technique, bool transpose_cols, int rank) {
    
    /*
        Initialize the solution board with the values of the original board.
    */

    Board solution;
    solution.grid = board.grid;
    solution.rows_count = board.rows_count;
    solution.cols_count = board.cols_count;
    solution.solution = (CellState *) malloc(board.rows_count * board.cols_count * sizeof(CellState));

    /*
        Combine the partial solutions from the rows and columns to get the final solution. 
    */

    if (rank == 0) {
        solution = combine_partial_solutions(board, row_solution, col_solution, technique, forced, transpose_cols);

        if (DEBUG) print_board(technique, solution, SOLUTION);
    }

    /*
        Share the final solution with all the processes.
    */

    MPI_Bcast(solution.solution, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);

    return solution;
}

#endif