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

    int i;
    int size = sizeof(pointers) / sizeof(pointers[0]);
    for (i = 0; i < size; i++) {
        free(pointers[i]);
    }
}

void mpi_share_board(Board* board, int rank) {

    /*
        Share the board with all the processes.
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