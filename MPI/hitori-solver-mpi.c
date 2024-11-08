#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_BUFFER_SIZE 2048
#define DEBUG true

enum State {
    UNKNOWN = -1,
    WHITE = 0,
    BLACK = 1
};

enum Type {
    BOARD = 0,
    SOLUTION = 1
};

enum ScatterType {
    ROWS = 0,
    COLS = 1
};

typedef struct Board {
    int *grid;
    int rows_count;
    int cols_count;
    int *solution;
} Board;

/* ------------------ HELPERS ------------------ */

// Helper function to read a matrix from the input file
void read_board(int **board, int *rows_count, int *cols_count, int **solution) {
    
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
            (*solution)[temp_row * cols + temp_col] = UNKNOWN;
            temp_col++;
        } while ((token = strtok(NULL, " ")));

        temp_row++;
    }

    fclose(fp);
} 

// Helper function to write the solution to the output file
void write_solution(struct Board board) {

    FILE *fp = fopen("./output.txt", "w");

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

// Helper function to print a vector
void print_vector(int *vector, int size) {
    int i;
    for (i = 0; i < size; i++) {
        printf("%d ", vector[i]);
    }
    printf("\n");
}

// Helper function to print the board
void print_board(char *title, struct Board board, enum Type type) {

    printf("\n# --- %s --- #\n", title);

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (type == BOARD) 
                printf("%d ", board.grid[i * board.cols_count + j]);
            else
                if (board.solution[i * board.cols_count + j] == WHITE) 
                    printf("O ");
                else if (board.solution[i * board.cols_count + j] == BLACK) 
                    printf("X ");
                else 
                    printf("? ");
        }
        printf("\n");
    }
}

// Helper function to transpose a matrix
struct Board transpose(struct Board board) {

    struct Board Tboard = board;
    Tboard.grid = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    Tboard.solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            Tboard.grid[j * board.rows_count + i] = board.grid[i * board.cols_count + j];
            Tboard.solution[j * board.rows_count + i] = board.solution[i * board.cols_count + j];
        }
    }

    return Tboard;
}

struct Board combine_board_solutions(struct Board first_board, struct Board second_board, bool forced) {

    struct Board final;

    final.grid = first_board.grid;
    final.rows_count = first_board.rows_count;
    final.cols_count = first_board.cols_count;
    final.solution = (int *) malloc(final.rows_count * final.cols_count * sizeof(int));

    int rows = final.rows_count;
    int cols = final.cols_count;

    int i, j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            if (first_board.solution[i * cols + j] == WHITE && second_board.solution[i * cols + j] == WHITE) final.solution[i * cols + j] = WHITE;
            else if (first_board.solution[i * cols + j] == BLACK && second_board.solution[i * cols + j] == BLACK) final.solution[i * cols + j] = BLACK;
            else if (first_board.solution[i * cols + j] == UNKNOWN && second_board.solution[i * cols + j] == UNKNOWN) final.solution[i * cols + j] = UNKNOWN;
            else if (!forced && first_board.solution[i * cols + j] == WHITE && second_board.solution[i * cols + j] == UNKNOWN) final.solution[i * cols + j] = WHITE;
            else if (!forced && first_board.solution[i * cols + j] == UNKNOWN && second_board.solution[i * cols + j] == WHITE) final.solution[i * cols + j] = WHITE;
            else if (!forced && first_board.solution[i * cols + j] == BLACK && second_board.solution[i * cols + j] == UNKNOWN) final.solution[i * cols + j] = BLACK;
            else if (!forced && first_board.solution[i * cols + j] == UNKNOWN && second_board.solution[i * cols + j] == BLACK) final.solution[i * cols + j] = BLACK;
            else final.solution[i * cols + j] = UNKNOWN;
        }   
    }

    return final;
}

struct Board combine_partial_solutions(struct Board board, int *row_solution, int *col_solution, char *technique, bool forced) {

    struct Board row_board = board;
    row_board.solution = row_solution;

    struct Board col_board = board;
    col_board.solution = col_solution;

    col_board = transpose(col_board);

    if (DEBUG) {
        printf("\n# --- %s --- #\n", technique);
        print_board("[Rows]", row_board, SOLUTION);
        print_board("[Cols]", col_board, SOLUTION);
    }

    return combine_board_solutions(row_board, col_board, forced);
}

/* ------------------ MPI UTILS ------------------ */

void mpi_scatter_board(struct Board board, int size, int rank, enum ScatterType type, int **local_vector, int **counts_send, int **displs_send) {

    *counts_send = (int *) malloc(size * sizeof(int)); // Number of elements to send to each process
    *displs_send = (int *) malloc(size * sizeof(int)); // Offset of each element to send

    /*
        Calculate the number of rows and columns to be assigned to each process.
        If there are more processes than rows or columns, assign 1 row or column to each process.
    */

    int item_per_process = (type == ROWS) ?
        (size > board.rows_count) ? 1 : board.rows_count / size :
        (size > board.cols_count) ? 1 : board.cols_count / size;

    int remaining_items = (type == ROWS) ?
        (size > board.rows_count) ? 0 : board.rows_count % size :
        (size > board.cols_count) ? 0 : board.cols_count % size;

    /*
        If the type is COLS, transpose the board to work with columns.
    */

    if (type == COLS) 
        board = transpose(board);
    
    //if (DEBUG && rank == 0) print_board("Transposed", board, BOARD);

    /*
        Divide the board in rows and columns and scatter them to each process,
        with the remaining rows and columns (if any) being assigned to the first process.
    */

    int i, j, k;
    int offset = 0;

    int total_processes = (type == ROWS) ?
        (size > board.rows_count) ? board.rows_count : size :
        (size > board.cols_count) ? board.cols_count : size;

    /*
        Calculate the number of elements to send to each process and the offset of each element.
    */

    for (i = 0; i < size; i++) {
        int items = (i == 0) ? item_per_process + remaining_items : item_per_process;
        (*counts_send)[i] = (i < total_processes) ?
            (type == ROWS) ? items * board.cols_count : items * board.rows_count : 0;
        (*displs_send)[i] = offset;
        offset += (*counts_send)[i];
    }

    /*
        Scatter the board, generating a local vector for each process.
    */

    *local_vector = (int *) malloc((*counts_send)[rank] * sizeof(int));

    MPI_Scatterv(board.grid, *counts_send, *displs_send, MPI_INT, *local_vector, (*counts_send)[rank], MPI_INT, 0, MPI_COMM_WORLD);
}

void mpi_gather_board(struct Board board, int rank, enum ScatterType type, int *local_vector, int *counts_send, int *displs_send, int **solution) {

    /*
        Gather the local vectors from each process and combine them to get the final solution.
    */

    *solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    MPI_Gatherv(local_vector, counts_send[rank], MPI_INT, *solution, counts_send, displs_send, MPI_INT, 0, MPI_COMM_WORLD);
}

/* ------------------ HITORI TECNIQUES ------------------ */

struct Board mpi_uniqueness_rule(struct Board board, int size, int rank) {

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, &local_col, &counts_send_col, &displs_send_col);
    
    int i, j, k;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    /*
        Initialize the local solutions with UNKNOWN values.
    */

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    // For each local row, check if there are unique values
    for (i = 0; i < (counts_send_row[rank] / board.cols_count); i++) {
        for (j = 0; j < board.cols_count; j++) {
            bool unique = true;
            int value = local_row[i * board.cols_count + j];

            for (k = 0; k < board.cols_count; k++) {
                if (j != k && value == local_row[i * board.cols_count + k]) {
                    unique = false;
                    break;
                }
            }

            if (unique)
                local_row_solution[i * board.cols_count + j] = WHITE; // If the value is unique, mark it as white
            else
                local_row_solution[i * board.cols_count + j] = UNKNOWN;
        }
    }

    // For each local column, check if there are unique values
    for (i = 0; i < (counts_send_col[rank] / board.rows_count); i++) {
        for (j = 0; j < board.rows_count; j++) {
            bool unique = true;
            int value = local_col[i * board.rows_count + j];

            for (k = 0; k < board.rows_count; k++) {
                if (j != k && value == local_col[i * board.rows_count + k]) {
                    unique = false;
                    break;
                }
            }

            if (unique)
                local_col_solution[i * board.rows_count + j] = WHITE; // If the value is unique, mark it as white
            else
                local_col_solution[i * board.rows_count + j] = UNKNOWN;
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, ROWS, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, COLS, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = {0};

    if (rank == 0)
        solution = combine_partial_solutions(board, row_solution, col_solution, "Uniqueness", true);

    free(local_row);
    free(counts_send_row);
    free(displs_send_row);
    free(local_col);
    free(counts_send_col);
    free(displs_send_col);
    free(row_solution);
    free(col_solution);

    return solution;
}

struct Board mpi_sandwich_rules(struct Board board, int size, int rank) {

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, &local_col, &counts_send_col, &displs_send_col);

    int i, j;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    // For each local row, check if triplet values are present
    for (i = 0; i < (counts_send_row[rank] / board.cols_count); i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 2) {
                int value1 = local_row[i * board.cols_count + j];
                int value2 = local_row[i * board.cols_count + j + 1];
                int value3 = local_row[i * board.cols_count + j + 2];

                // 222 --> XOX
                // 212 --> ?O?
                if (local_row_solution[i * board.cols_count + j] == UNKNOWN &&
                    local_row_solution[i * board.cols_count + j + 1] == UNKNOWN &&
                    local_row_solution[i * board.cols_count + j + 2] == UNKNOWN) {

                    if (value1 == value2 && value2 == value3) {
                        local_row_solution[i * board.cols_count + j] = BLACK;
                        local_row_solution[i * board.cols_count + j + 1] = WHITE;
                        local_row_solution[i * board.cols_count + j + 2] = BLACK;
                    } else if (value1 != value2 && value1 == value3) {
                        local_row_solution[i * board.cols_count + j] = UNKNOWN;
                        local_row_solution[i * board.cols_count + j + 1] = WHITE;
                        local_row_solution[i * board.cols_count + j + 2] = UNKNOWN;
                    } 
                }
            }
        }
    }

    // For each local column, check if triplet values are present
    for (i = 0; i < (counts_send_col[rank] / board.rows_count); i++) {
        for (j = 0; j < board.rows_count; j++) {
            if (j < board.rows_count - 2) {
                int value1 = local_col[i * board.rows_count + j];
                int value2 = local_col[i * board.rows_count + j + 1];
                int value3 = local_col[i * board.rows_count + j + 2];

                // 222 --> XOX
                if (local_col_solution[i * board.rows_count + j] == UNKNOWN &&
                    local_col_solution[i * board.rows_count + j + 1] == UNKNOWN &&
                    local_col_solution[i * board.rows_count + j + 2] == UNKNOWN) {

                    if (value1 == value2 && value2 == value3) {
                        local_col_solution[i * board.rows_count + j] = BLACK;
                        local_col_solution[i * board.rows_count + j + 1] = WHITE;
                        local_col_solution[i * board.rows_count + j + 2] = BLACK;
                    } else if (value1 != value2 && value1 == value3) {
                        local_col_solution[i * board.rows_count + j] = UNKNOWN;
                        local_col_solution[i * board.rows_count + j + 1] = WHITE;
                        local_col_solution[i * board.rows_count + j + 2] = UNKNOWN;
                    }
                }
            }
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, ROWS, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, COLS, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = {0};

    if (rank == 0)
        solution = combine_partial_solutions(board, row_solution, col_solution, "Sandwich", false);
    
    free(local_row);
    free(counts_send_row);
    free(displs_send_row);
    free(local_col);
    free(counts_send_col);
    free(displs_send_col);
    free(row_solution);
    free(col_solution);

    return solution;
}

void mpi_isolated_black(); // Neighbors of black cells are white ?????

void mpi_pair_isolation(); // 22??2? --> 22?OXO

void mpi_flanked_isolation(); // 2332 2 3 --> 2332?X?X

void mpi_corner_cases();

/* ------------------ MAIN ------------------ */

int main(int argc, char** argv) {

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    struct Board board;

    if (rank == 0) {
        read_board(&board.grid, &board.rows_count, &board.cols_count, &board.solution);

        if (DEBUG) print_board("Initial", board, BOARD);
    }

    // TODO: Convert to MPI Datatype

    MPI_Bcast(&board.rows_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&board.cols_count, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank != 0) {
        board.grid = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
        board.solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    }

    MPI_Bcast(board.grid, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board.solution, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);

    /*
        TODO:
            1) Divide the columns and rows with Scatterv
            2) For each technique, apply it to the local rows and columns
            3) Gather the results with Gatherv
            4) Combine the partial solutions with bitwise operations
            5) Repeat until the solution doesn't change
            6) Backtrack to complete the board
    */

    /*
        Apply the techniques to the board.
    */

    struct Board uniqueness_solution = mpi_uniqueness_rule(board, size, rank);

    if (DEBUG && rank == 0) print_board("Uniqueness Rule [Combined]", uniqueness_solution, SOLUTION);

    struct Board sandwich_solution = mpi_sandwich_rules(board, size, rank);

    if (DEBUG && rank == 0) print_board("Sandwich Rules [Combined]", sandwich_solution, SOLUTION);

    struct Board final_solution = combine_board_solutions(uniqueness_solution, sandwich_solution, false);

    if (DEBUG && rank == 0) print_board("Final", final_solution, SOLUTION);

    if (rank == 0) {
        write_solution(final_solution);
    }

    MPI_Finalize();

    return 0;
}