#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../include/utils.h"
#include "../include/board.h"

void write_solution(Board board) {

    /*
        Helper function to write the solution to the output file.
    */

    /*
        Parameters:
            - board: The board to write the solution.
    */

    FILE *fp = fopen("./output/output.txt", "w");

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == WHITE) 
                fprintf(fp, "O ");
            else if (board.solution[i * board.cols_count + j] == BLACK) 
                fprintf(fp, "X ");
            else 
                fprintf(fp, "? ");
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
}

void print_vector(int *vector, int size) {

    /*
        Helper function to print a vector.
    */

    /*
        Parameters:
            - vector: The vector to print.
            - size: The size of the vector.
    */

    int i;
    for (i = 0; i < size; i++) {
        printf("%d ", vector[i]);
    }
    printf("\n");
}

void print_block(Board board, char *title, BCB* block) {
    
    /*
        Helper function to print the block.
    */

    /*
        Parameters:
            - board: The board.
            - title: The title of the block.
            - block: The block to print.
    */

    char buffer[MAX_BUFFER_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "# --- %s --- #\n", title);
    
    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (block->solution[i * board.cols_count + j] == WHITE) 
                strncat(buffer, "O ", sizeof(buffer) - strlen(buffer) - 1);
            else if (block->solution[i * board.cols_count + j] == BLACK) 
                strncat(buffer, "X ", sizeof(buffer) - strlen(buffer) - 1);
            else 
                strncat(buffer, "? ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    strncat(buffer, "\n --- Unknowns --- \n", sizeof(buffer) - strlen(buffer) - 1);

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            char unknown[3];
            snprintf(unknown, sizeof(unknown), "%d ", block->solution_space_unknowns[i * board.cols_count + j]);
            strncat(buffer, unknown, sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    printf("%s", buffer);
    
}

void free_memory(int *pointers[]) {

    /*
        Helper function to free the memory allocated for all the pointers.
    */

    /*
        Parameters:
            - pointers: The array of pointers to free.
    */

    int i;
    int size = sizeof(pointers) / sizeof(pointers[0]);
    for (i = 0; i < size; i++) {
        free(pointers[i]);
    }
}

void mpi_share_board(Board* board, int rank) {

    /*
        Share the board with all the processes from the manager process.
    */

    /*
        Parameters:
            - board: The board to share.
            - rank: The rank of the process.
    */

    MPI_Bcast(&board->rows_count, 1, MPI_INT, MANAGER_RANK, MPI_COMM_WORLD);
    MPI_Bcast(&board->cols_count, 1, MPI_INT, MANAGER_RANK, MPI_COMM_WORLD);
    
    if (rank != MANAGER_RANK) {
        board->grid = (int *) malloc(board->rows_count * board->cols_count * sizeof(int));
        board->solution = (CellState *) malloc(board->rows_count * board->cols_count * sizeof(CellState));
    }

    MPI_Bcast(board->grid, board->rows_count * board->cols_count, MPI_INT, MANAGER_RANK, MPI_COMM_WORLD);
    MPI_Bcast(board->solution, board->rows_count * board->cols_count, MPI_INT, MANAGER_RANK, MPI_COMM_WORLD);
}

void mpi_scatter_board(Board board, int rank, int size, ScatterType scatter_type, BoardType target_type, int **local_vector, int **counts_send, int **displs_send, MPI_Comm PRUNING_COMM) {

    /*
        Initialize the counts_send and displs_send arrays:
            1) The first array will store the number of elements to send to each process, 
            2) The second array will store the offset of each element.
    */

    *counts_send = (int *) malloc(size * sizeof(int));
    *displs_send = (int *) malloc(size * sizeof(int));

    /*
        Calculate the number of rows and columns to be assigned to each process.
        If there are more processes than rows or columns, assign 1 row or column to each process.
    */

    int item_per_process = (scatter_type == ROWS) ?
        (size > board.rows_count) ? 1 : board.rows_count / size :
        (size > board.cols_count) ? 1 : board.cols_count / size;

    int remaining_items = (scatter_type == ROWS) ?
        (size > board.rows_count) ? 0 : board.rows_count % size :
        (size > board.cols_count) ? 0 : board.cols_count % size;

    /*
        If the scatter_type is COLS, transpose the board to work with columns.
    */

    if (scatter_type == COLS) 
        board = transpose(board);

    /*
        Divide the board in rows and columns and scatter them to each process,
        with the remaining rows and columns (if any) being assigned to the first process.
    */

    int i, offset = 0;

    int total_processes = (scatter_type == ROWS) ?
        (size > board.rows_count) ? board.rows_count : size :
        (size > board.cols_count) ? board.cols_count : size;

    /*
        Calculate the number of elements to send to each process and the offset of each element.
    */

    for (i = 0; i < size; i++) {
        int items = (i == 0) ? item_per_process + remaining_items : item_per_process;
        (*counts_send)[i] = (i < total_processes) ?
            (scatter_type == ROWS) ? items * board.cols_count : items * board.rows_count : 0;
        (*displs_send)[i] = offset;
        offset += (*counts_send)[i];
    }

    /*
        Scatter the board, generating a local vector for each process based on the target_type.
        If the target_type is BOARD, scatter the board, otherwise scatter the solution.
    */

    *local_vector = (int *) malloc((*counts_send)[rank] * sizeof(int));

    (target_type == BOARD) ?
        MPI_Scatterv(board.grid, *counts_send, *displs_send, MPI_INT, *local_vector, (*counts_send)[rank], MPI_INT, 0, PRUNING_COMM) :
        MPI_Scatterv(board.solution, *counts_send, *displs_send, MPI_INT, *local_vector, (*counts_send)[rank], MPI_INT, 0, PRUNING_COMM);
}

void mpi_gather_board(Board board, int rank, int *local_vector, int *counts_send, int *displs_send, int **solution, MPI_Comm PRUNING_COMM) {

    /*
        Gather the local vectors from each process and combine them to get the final solution.
    */

    *solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    MPI_Gatherv(local_vector, counts_send[rank], MPI_INT, *solution, counts_send, displs_send, MPI_INT, MANAGER_RANK, PRUNING_COMM);
}
