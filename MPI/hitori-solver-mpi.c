#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_BUFFER_SIZE 2048
#define DEBUG false

enum State {
    UNASSIGNED = -1,
    WHITE = 0,
    BLACK = 1
};

enum Type {
    BOARD = 0,
    SOLUTION = 1
};

// Helper function to read a matrix from the input file
void read_board(int **board, int *rows_count, int *cols_count) {
    
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

    rewind(fp);

    int temp_row = 0, temp_col;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        char *token = strtok(line, " ");

        temp_col = 0;
        do {  
            (*board)[temp_row * cols + temp_col] = atoi(token);
            temp_col++;
        } while ((token = strtok(NULL, " ")));

        temp_row++;
    }

    fclose(fp);
} 

// Helper function to print a vector
void print_vector(int *vector, int size) {
    int i;
    for (i = 0; i < size; i++) {
        printf("%d ", vector[i]);
    }
    printf("\n");
}

// Helper function to print the board
void print_board(int *board, int rows_count, int cols_count, enum Type type) {

    if (DEBUG) printf("\nRows: %d, Cols: %d\n", rows_count, cols_count);

    int i, j;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            if (type == BOARD) 
                printf("%d ", board[i * cols_count + j]);
            else
                if (board[i * cols_count + j] == WHITE) 
                    printf("O ");
                else if (board[i * cols_count + j] == BLACK) 
                    printf("X ");
                else 
                    printf("? ");
        }
        printf("\n");
    }

    printf("\n");

    fflush(stdout);
}

void transpose(int *board, int rows_count, int cols_count, int **Tboard) {
    *Tboard = (int *) malloc(rows_count * cols_count * sizeof(int));

    int i, j;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            (*Tboard)[j * rows_count + i] = board[i * cols_count + j];
        }
    }
}

void mpi_find_unique_values(int rank, int size, int *board, int rows_count, int cols_count, int **solution) {

    int counts_send_row[size], counts_send_col[size]; // Number of elements to send to each process
    int displs_send_row[size], displs_send_col[size]; // Offset of each element to send

    /*
        Calculate the number of rows and columns to be assigned to each process.
        If there are more processes than rows or columns, assign 1 row or column to each process.
    */

    int rows_per_process = (size > rows_count) ? 1 : rows_count / size;
    int remaining_rows = (size > rows_count) ? 0 : rows_count % size; 

    int cols_per_process = (size > rows_count) ? 1 : cols_count / size;
    int remaining_cols = (size > rows_count) ? 0 : cols_count % size;

    int *Tboard;
    transpose(board, rows_count, cols_count, &Tboard); // Transpose the board to work with columns

    /*
        Divide the board in rows and columns and scatter them to each process,
        with the remaining rows and columns (if any) being assigned to the first process.
    */

    int i, j, k;
    int offset_row = 0, offset_col = 0;
    
    int total_row_processes = (size > rows_count) ? rows_count : size;
    int total_col_processes = (size > cols_count) ? cols_count : size;
    for (i = 0; i < size; i++) {
        int rows = (i == 0) ? rows_per_process + remaining_rows : rows_per_process;
        int cols = (i == 0) ? cols_per_process + remaining_cols : cols_per_process;
        counts_send_row[i] = (i < total_row_processes) ? rows * cols_count : 0;
        counts_send_col[i] = (i < total_col_processes) ? cols * rows_count : 0;
        displs_send_row[i] = offset_row;
        displs_send_col[i] = offset_col;
        offset_row += counts_send_row[i];
        offset_col += counts_send_col[i];
    }

    int local_row[counts_send_row[rank]];
    int local_col[counts_send_col[rank]];

    MPI_Scatterv(Tboard, counts_send_col, displs_send_col, MPI_INT, local_col, counts_send_col[rank], MPI_INT, 0, MPI_COMM_WORLD); // Scatter the board in cols
    MPI_Scatterv(board, counts_send_row, displs_send_row, MPI_INT, local_row, counts_send_row[rank], MPI_INT, 0, MPI_COMM_WORLD); // Scatter the board in rows
    
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    // For each local row, check if there are unique values
    for (i = 0; i < (counts_send_row[rank] / cols_count); i++) {
        for (j = 0; j < cols_count; j++) {
            bool unique = true;
            int value = local_row[i * cols_count + j];

            for (k = 0; k < cols_count; k++) {
                if (j != k && value == local_row[i * cols_count + k]) {
                    unique = false;
                    break;
                }
            }

            if (unique)
                local_row_solution[i * cols_count + j] = WHITE; // If the value is unique, mark it as white
            else
                local_row_solution[i * cols_count + j] = UNASSIGNED;
        }
    }

    // For each local column, check if there are unique values
    for (i = 0; i < (counts_send_col[rank] / rows_count); i++) {
        for (j = 0; j < rows_count; j++) {
            bool unique = true;
            int value = local_col[i * rows_count + j];

            for (k = 0; k < rows_count; k++) {
                if (j != k && value == local_col[i * rows_count + k]) {
                    unique = false;
                    break;
                }
            }

            if (unique)
                local_col_solution[i * rows_count + j] = WHITE; // If the value is unique, mark it as white
            else
                local_col_solution[i * rows_count + j] = UNASSIGNED;
        }
    }

    int *row_solution = (int *) malloc(rows_count * cols_count * sizeof(int));
    int *Tsolution = (int *) malloc(rows_count * cols_count * sizeof(int));

    MPI_Gatherv(local_row_solution, counts_send_row[rank], MPI_INT, row_solution, counts_send_row, displs_send_row, MPI_INT, 0, MPI_COMM_WORLD); // Gather the solution for rows
    MPI_Gatherv(local_col_solution, counts_send_col[rank], MPI_INT, Tsolution, counts_send_col, displs_send_col, MPI_INT, 0, MPI_COMM_WORLD); // Gather the solution for cols

    int *unique_solution = (int *) malloc(rows_count * cols_count * sizeof(int));

    if (rank == 0) {
        int *col_solution = (int *) malloc(rows_count * cols_count * sizeof(int));
        transpose(Tsolution, rows_count, cols_count, &col_solution); // Transpose again to get the solution for columns

        for (i = 0; i < rows_count; i++) {
            for (j = 0; j < cols_count; j++) {
                if (row_solution[i * cols_count + j] == WHITE && col_solution[i * cols_count + j] == WHITE) // If the value is unique in both rows and columns, mark it as white
                    unique_solution[i * cols_count + j] = WHITE;
                else
                    unique_solution[i * cols_count + j] = UNASSIGNED;
            }
        }

        free(col_solution);
    }

    // Free memory
    free(Tboard);
    free(row_solution);
    free(Tsolution);

    MPI_Bcast(unique_solution, rows_count * cols_count, MPI_INT, 0, MPI_COMM_WORLD); // Broadcast the unique solution

    *solution = unique_solution; // Update the solution pointer for all processes

    free(unique_solution);
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (DEBUG) printf("Process [%d]", rank);

    int *board; // Original puzzle board
    int *solution; // Solution board
    int rows_count, cols_count;

    if (rank == 0) {
        read_board(&board, &rows_count, &cols_count);
    }

    MPI_Bcast(&rows_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&cols_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board, rows_count * cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    
    //if (DEBUG) print_board(board, rows_count, cols_count, BOARD);

    mpi_find_unique_values(rank, size, board, rows_count, cols_count, &solution);

    if (rank == 0) print_board(solution, rows_count, cols_count, SOLUTION);

    MPI_Finalize();
    return 0;
}