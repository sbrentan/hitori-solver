#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_BUFFER_SIZE 2048
#define DEBUG true

enum State {
    UNASSIGNED = -1,
    WHITE = 0,
    BLACK = 1
};

// Helper function to read a matrix from the input file
void read_board(int **board, int **solution, int *rows_count, int *cols_count) {
    FILE *fp = fopen("../test-cases/inputs/input-5x5.txt", "r");
    
    if (fp == NULL) {
        printf("Could not open file.\n");
        exit(1);
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

    *rows_count = rows;
    *cols_count = cols;

    *board = (int *) malloc(rows * cols * sizeof(int));
    *solution = (int *) malloc(rows * cols * sizeof(int));

    rewind(fp);

    int temp_row = 0, temp_col;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        char *token = strtok(line, " ");

        temp_col = 0;
        do {  
            (*board)[temp_row * cols + temp_col] = atoi(token);
            (*solution)[temp_row * cols + temp_col] = UNASSIGNED;
            temp_col++;
        } while ((token = strtok(NULL, " ")));

        temp_row++;
    }

    fclose(fp);
} 

// Helper function to print the board
void print_board(int *board, int rows_count, int cols_count) {

    if (DEBUG) printf("\nRows: %d, Cols: %d\n", rows_count, cols_count);

    int i, j;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            printf("%d ", board[i * cols_count + j]);
        }
        printf("\n");
    }

    printf("\n");

    fflush(stdout);
}

// Helper function to print the solution
/*void print_solution() {

    if(DEBUG) printf("\nRows: %d, Cols: %d\n", rows_count, cols_count);

    int i, j;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            if (solution[i * cols_count + j] == WHITE) {
                printf("O ");
            } else if (solution[i * cols_count + j] == BLACK) {
                printf("X ");
            } else {
                printf("? ");
            }
        }
        printf("\n");
    }

    fflush(stdout);
}

void find_unique_values() {

    int i, j, k;    
    for(i = 0; i < rows_count; i++) {
        for(j = 0; j < cols_count; j++) {
            int value = board[i * cols_count + j];
            bool unique = true;

            // Check row
            for(k = 0; k < cols_count; k++) {
                if(k != j && board[i * cols_count + k] == value) {
                    unique = false;
                    break;
                }
            }

            if(unique) {
                // Check column
                for(k = 0; k < rows_count; k++) {
                    if(k != i && board[k * cols_count + j] == value) {
                        unique = false;
                        break;
                    }
                }
            }

            if (unique)
                solution[i * cols_count + j] = WHITE;
        }
    }
}

void parallel_find_unique_values() {


    MPI_Bcast(&rows_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&cols_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board, rows_count * cols_count, MPI_INT, 0, MPI_COMM_WORLD);

    int rows_per_proc = rows_count / size;
    int start_row = rank * rows_per_proc;
    int end_row = (rank == size - 1) ? rows_count : start_row + rows_per_proc;

    for(int i = start_row; i < end_row; i++) {
        for(int j = 0; j < cols_count; j++) {
            int value = board[i * cols_count + j];
            bool unique = true;

            for(int k = 0; k < cols_count; k++) {
                if(k != j && board[i * cols_count + k] == value) {
                    unique = false;
                    break;
                }
            }

            if(unique) {
                for(int k = 0; k < rows_count; k++) {
                    if(k != i && board[k * cols_count + j] == value) {
                        unique = false;
                        break;
                    }
                }
            }

            solution[i * cols_count + j] = unique ? WHITE : solution[i * cols_count + j];
        }
    }

    MPI_Gather(&solution[start_row * cols_count], (end_row - start_row) * cols_count, MPI_INT,
               solution, (end_row - start_row) * cols_count, MPI_INT,
               0, MPI_COMM_WORLD);
}*/

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int *board; // Original puzzle board
    int *solution;
    int rows_count, cols_count;

    if (rank == 0) {
        read_board(&board, &solution, &rows_count, &cols_count);
    }

    MPI_Bcast(&rows_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&cols_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board, rows_count * cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (DEBUG) printf("Process [%d]", rank);
    print_board(board, rows_count, cols_count);

    MPI_Finalize();
    return 0;
}