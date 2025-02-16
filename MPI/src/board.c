#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/board.h"

void read_board(Board* board, char *filename) {

    /*
        Helper function to read the board from the input file.
    */

    /*
        Parameters:
            - board: the board to be read
            - filename: the name of the input file
    */

    char path[MAX_BUFFER_SIZE];
    snprintf(path, sizeof(path), "%s%s", INPUT_PATH, filename);
    
    FILE *fp = fopen(path, "r");
    
    if (fp == NULL) {
        fprintf(stderr, "Could not open file.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    char line[MAX_BUFFER_SIZE];
    
    int rows = 0, cols = 0;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        rows++;

        if (rows == 1) {
            char *token = strtok(line, " ");
            do {
                cols++;
            } while ((token = strtok(NULL, " ")));
        }
    }

    board->rows_count = rows;
    board->cols_count = cols;

    if (DEBUG) {
        printf("Rows: %d\n", rows);
        printf("Cols: %d\n", cols);
    }

    if (rows != cols) {
        printf("The board must be a square.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    board->grid = (int *) malloc(rows * cols * sizeof(int));
    board->solution = (int *) malloc(rows * cols * sizeof(CellState));

    rewind(fp);

    int temp_row = 0, temp_col;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        char *token = strtok(line, " ");

        temp_col = 0;
        do {  
            board->grid[temp_row * cols + temp_col] = atoi(token);
            board->solution[temp_row * cols + temp_col] = UNKNOWN;
            temp_col++;
        } while ((token = strtok(NULL, " ")));

        temp_row++;
    }

    fclose(fp);
} 

void print_board(char *title, Board board, BoardType type) {

    /*
        Helper function to print the board.
    */

    /*
        Parameters:
            - title: the title of the board
            - board: the board to be printed
            - type: the type of the board (BOARD or SOLUTION)
    */
    
    char buffer[MAX_BUFFER_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "\n# --- %s --- #\n", title);

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (type == BOARD) 
                snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d ", board.grid[i * board.cols_count + j]);
            else
                if (board.solution[i * board.cols_count + j] == WHITE) 
                    strncat(buffer, "O ", sizeof(buffer) - strlen(buffer) - 1);
                else if (board.solution[i * board.cols_count + j] == BLACK) 
                    strncat(buffer, "X ", sizeof(buffer) - strlen(buffer) - 1);
                else 
                    strncat(buffer, "? ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }
    strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    printf("%s", buffer);
}

bool is_board_solution_equal(Board first_board, Board second_board) {
    /*
        Helper function to check if two boards are equal.
    */

    /*
        Parameters:
            - first_board: the first board to be compared
            - second_board: the second board to be compared
    */

    if (first_board.rows_count != second_board.rows_count || first_board.cols_count != second_board.cols_count) return false;

    int i, j;
    for (i = 0; i < first_board.rows_count; i++)
        for (j = 0; j < first_board.cols_count; j++)
            if (first_board.solution[i * first_board.cols_count + j] != second_board.solution[i * first_board.cols_count + j]) return false;

    return true;
}

Board transpose(Board board) {
    /*
        Helper function to transpose a board.
    */

    /*
        Parameters:
            - board: the board to be transposed
    */

    Board Tboard = { (int *) malloc(board.rows_count * board.cols_count * sizeof(int)), board.cols_count, board.rows_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            Tboard.grid[j * board.rows_count + i] = board.grid[i * board.cols_count + j];
            Tboard.solution[j * board.rows_count + i] = board.solution[i * board.cols_count + j];
        }
    }

    return Tboard;
}

Board combine_boards(Board first_board, Board second_board, bool forced, int rank, char *technique, MPI_Comm PRUNING_COMM) {
    
    /*
        Helper function to combine two boards.
    */

    /*
        Parameters:
            - first_board: the first board to be combined
            - second_board: the second board to be combined
            - forced: if the technique is forced, the values must be the same
            - rank: the rank of the process
            - technique: the name of the technique
            - PRUNING_COMM: the MPI communicator dedicated to the pruning workers
    */

    int rows = -1, cols = -1;

    if (rank == MANAGER_RANK) {
        rows = first_board.rows_count;
        cols = first_board.cols_count;
    }

    /*
        Broadcast the dimensions of the boards.
    */

    MPI_Bcast(&rows, 1, MPI_INT, MANAGER_RANK, PRUNING_COMM);
    MPI_Bcast(&cols, 1, MPI_INT, MANAGER_RANK, PRUNING_COMM);

    /*
        Initialize the solution board with the values of the original board.
    */

    Board merged = { (int *) malloc(rows * cols * sizeof(int)), rows, cols, (int *) malloc(rows * cols * sizeof(int)) };

    if (rank == MANAGER_RANK) merged.grid = first_board.grid;

    MPI_Bcast(merged.grid, rows * cols, MPI_INT, MANAGER_RANK, PRUNING_COMM);

    /*
        Combine the solutions by performing a pairwise comparison:
            1) If the values are the same, keep the value
            2) If only one value is unknown, keep the known value
            3) If the values are different, leave the cell as unknown
        
        Forced techniques must require the values to be the same.
    */

    if (rank == MANAGER_RANK) {

        memset(merged.solution, UNKNOWN, rows * cols * sizeof(int));

        int i, j;
        for (i = 0; i < rows; i++) {
            for (j = 0; j < cols; j++) {
                if (first_board.solution[i * cols + j] == second_board.solution[i * cols + j]) 
                    merged.solution[i * cols + j] = first_board.solution[i * cols + j];
                else if (!forced && first_board.solution[i * cols + j] == UNKNOWN && second_board.solution[i * cols + j] != UNKNOWN) 
                    merged.solution[i * cols + j] = second_board.solution[i * cols + j];
                else if (!forced && first_board.solution[i * cols + j] != UNKNOWN && second_board.solution[i * cols + j] == UNKNOWN) 
                    merged.solution[i * cols + j] = first_board.solution[i * cols + j];
            }   
        }
    }

    /*
        Broadcast the merged solution.
    */
    
    MPI_Bcast(merged.solution, rows * cols, MPI_INT, MANAGER_RANK, PRUNING_COMM);

    return merged;
}
