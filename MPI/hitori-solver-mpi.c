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
    
    FILE *fp = fopen("../test-cases/inputs/input-8x8.txt", "r");
    
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
    memset(final.solution, UNKNOWN, final.rows_count * final.cols_count * sizeof(int));

    int rows = final.rows_count;
    int cols = final.cols_count;

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

struct Board combine_partial_solutions(struct Board board, int *row_solution, int *col_solution, char *technique, bool forced, bool transpose_cols) {

    struct Board row_board = board;
    row_board.solution = row_solution;

    struct Board col_board = board;
    col_board.solution = col_solution;

    if (transpose_cols)
        col_board = transpose(col_board);

    if (DEBUG) {
        printf("\n# --- %s --- #\n", technique);
        print_board("[Rows]", row_board, SOLUTION);
        print_board("[Cols]", col_board, SOLUTION);
    }

    return combine_board_solutions(row_board, col_board, forced);
}

void free_memory(int *pointers[]) {
    int i;
    int size = sizeof(pointers) / sizeof(pointers[0]);
    for (i = 0; i < size; i++) {
        free(pointers[i]);
    }
}

/* ------------------ MPI UTILS ------------------ */

void mpi_scatter_board(struct Board board, int size, int rank, enum ScatterType scatter_type, enum Type target_type, int **local_vector, int **counts_send, int **displs_send) {

    *counts_send = (int *) malloc(size * sizeof(int)); // Number of elements to send to each process
    *displs_send = (int *) malloc(size * sizeof(int)); // Offset of each element to send

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
    
    //if (DEBUG && rank == 0) print_board("Transposed", board, BOARD);

    /*
        Divide the board in rows and columns and scatter them to each process,
        with the remaining rows and columns (if any) being assigned to the first process.
    */

    int i, j, k;
    int offset = 0;

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
        Scatter the board, generating a local vector for each process.
    */

    *local_vector = (int *) malloc((*counts_send)[rank] * sizeof(int));

    (target_type == BOARD) ?
        MPI_Scatterv(board.grid, *counts_send, *displs_send, MPI_INT, *local_vector, (*counts_send)[rank], MPI_INT, 0, MPI_COMM_WORLD) :
        MPI_Scatterv(board.solution, *counts_send, *displs_send, MPI_INT, *local_vector, (*counts_send)[rank], MPI_INT, 0, MPI_COMM_WORLD);
}

void mpi_gather_board(struct Board board, int rank, int *local_vector, int *counts_send, int *displs_send, int **solution) {

    /*
        Gather the local vectors from each process and combine them to get the final solution.
    */

    *solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    MPI_Gatherv(local_vector, counts_send[rank], MPI_INT, *solution, counts_send, displs_send, MPI_INT, 0, MPI_COMM_WORLD);
}

struct Board mpi_compute_and_share(struct Board board, int rank, int *row_solution, int *col_solution, bool forced, char *technique, bool transpose_cols) {
    
    struct Board solution;

    solution.grid = board.grid;
    solution.rows_count = board.rows_count;
    solution.cols_count = board.cols_count;
    solution.solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    if (rank == 0) {
        solution = combine_partial_solutions(board, row_solution, col_solution, technique, forced, transpose_cols);

        if (DEBUG) print_board(technique, solution, SOLUTION);
    }

    MPI_Bcast(solution.solution, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);

    return solution;
}

/* ------------------ HITORI TECNIQUES ------------------ */

struct Board mpi_uniqueness_rule(struct Board board, int size, int rank) {

    /*
        RULE DESCRIPTION:
        
        If a value is unique in a row or column, mark it as white.

        e.g. 2 3 2 1 1 --> 2 O 2 1 1
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);
    
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
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = mpi_compute_and_share(board, rank, row_solution, col_solution, true, "Uniqueness Rule", true);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

struct Board mpi_set_white(struct Board board, int size, int rank) {
    
    /*
        RULE DESCRIPTION:
        
        When you have whited a cell, you can mark all the other cells with the same number in the row or column as black
        
        e.g. Suppose a whited 3. Then 2 O ... 3 --> 2 O ... X
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, SOLUTION, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, SOLUTION, &local_col, &counts_send_col, &displs_send_col);

    int i, j, k;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    if (rank == 1)
        print_vector(local_row, counts_send_row[rank]);
    
    // For each local row, check if there is a white cell
    int rows_count = (counts_send_row[rank] / board.cols_count);
    for (i = 0; i < rows_count; i++) {
        int grid_row_index = rank * rows_count * board.cols_count;
        for (j = 0; j < board.cols_count; j++) {
            if (local_row[i * board.cols_count + j] == WHITE) {
                int value = board.grid[grid_row_index + j];

                for (k = 0; k < board.cols_count; k++) {
                    if (board.grid[grid_row_index + k] == value && k != j) {
                        local_row_solution[i * board.cols_count + k] = BLACK;

                        if (k - 1 >= 0) local_row_solution[i * board.cols_count + k - 1] = WHITE;
                        if (k + 1 < board.cols_count) local_row_solution[i * board.cols_count + k + 1] = WHITE;
                    }
                }
            }
        }
    }

    if (rank == 1)
        print_vector(local_row_solution, counts_send_col[rank]);

    struct Board TBoard = transpose(board);

    // For each local column, check if triplet values are present
    int cols_count = (counts_send_col[rank] / board.rows_count);
    for (i = 0; i < cols_count; i++) {
        int grid_col_index = rank * cols_count * board.rows_count;
        for (j = 0; j < board.rows_count; j++) {
            if (local_col[i * board.rows_count + j] == WHITE) {
                int value = TBoard.grid[grid_col_index + j];

                for (k = 0; k < board.rows_count; k++) {
                    if (TBoard.grid[grid_col_index + k] == value && k != j) {
                        local_col_solution[i * board.rows_count + k] = BLACK;

                        if (k - 1 >= 0) local_col_solution[i * board.rows_count + k - 1] = WHITE;
                        if (k + 1 < board.rows_count) local_col_solution[i * board.rows_count + k + 1] = WHITE;
                    }
                }
            }
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = mpi_compute_and_share(board, rank, row_solution, col_solution, false, "Set White", true);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

struct Board mpi_set_black(struct Board board, int size, int rank) {
    
    /*
        RULE DESCRIPTION:
        
        When you have marked a black cell, all the cells around it must be white.

        e.g. 2 X 2 --> O X O
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, SOLUTION, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, SOLUTION, &local_col, &counts_send_col, &displs_send_col);

    int i, j, k;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    // For each local row, check if there is a white cell
    for (i = 0; i < (counts_send_row[rank] / board.cols_count); i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (local_row[i * board.cols_count + j] == BLACK) {                
                if (j - 1 >= 0) local_row_solution[i * board.cols_count + j - 1] = WHITE;
                if (j + 1 < board.cols_count) local_row_solution[i * board.cols_count + j + 1] = WHITE;
            }
        }
    }

    // For each local column, check if triplet values are present
    for (i = 0; i < (counts_send_col[rank] / board.rows_count); i++) {
        for (j = 0; j < board.rows_count; j++) {
            if (local_col[i * board.rows_count + j] == BLACK) {
                if (j - 1 >= 0) local_col_solution[i * board.rows_count + j - 1] = WHITE;
                if (j + 1 < board.rows_count) local_col_solution[i * board.rows_count + j + 1] = WHITE;
            }
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = mpi_compute_and_share(board, rank, row_solution, col_solution, false, "Set Black", true);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

struct Board mpi_sandwich_rules(struct Board board, int size, int rank) {

    /*
        RULE DESCRIPTION:
        
        1) Sandwich Triple: If you have three on a row, mark the edges as black and the middle as white

        e.g. 2 2 2 --> X O X
        
        2) Sandwich Pair: If you have two similar numbers with one between, you can mark the middle as white

        e.g. 2 3 2 --> 2 O 2
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);

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
                if (value1 == value2 && value2 == value3) {
                    local_row_solution[i * board.cols_count + j] = BLACK;
                    local_row_solution[i * board.cols_count + j + 1] = WHITE;
                    local_row_solution[i * board.cols_count + j + 2] = BLACK;

                    if (j - 1 >= 0) local_col_solution[i * board.rows_count + j - 1] = WHITE;
                    if (j + 3 < board.rows_count) local_col_solution[i * board.rows_count + j + 3] = WHITE;

                } else if (value1 != value2 && value1 == value3) {
                    local_row_solution[i * board.cols_count + j] = UNKNOWN;
                    local_row_solution[i * board.cols_count + j + 1] = WHITE;
                    local_row_solution[i * board.cols_count + j + 2] = UNKNOWN;
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
                if (value1 == value2 && value2 == value3) {
                    local_col_solution[i * board.rows_count + j] = BLACK;
                    local_col_solution[i * board.rows_count + j + 1] = WHITE;
                    local_col_solution[i * board.rows_count + j + 2] = BLACK;

                    if (j - 1 >= 0) local_col_solution[i * board.rows_count + j - 1] = WHITE;
                    if (j + 3 < board.rows_count) local_col_solution[i * board.rows_count + j + 3] = WHITE;

                } else if (value1 != value2 && value1 == value3) {
                    local_col_solution[i * board.rows_count + j] = UNKNOWN;
                    local_col_solution[i * board.rows_count + j + 1] = WHITE;
                    local_col_solution[i * board.rows_count + j + 2] = UNKNOWN;
                }
            }
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = mpi_compute_and_share(board, rank, row_solution, col_solution, false, "Sandwich Rules", true);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

struct Board mpi_pair_isolation(struct Board board, int size, int rank) {

    /*
        RULE DESCRIPTION:
        
        If you have a double and some singles, you can mark all the singles as black

        e.g. 2 2 ... 2 ... 2 --> 2 2 ... X ... X
    */
    
    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, size, rank, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, size, rank, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);

    int i, j, k;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    // For each local row, check if there are pairs of values
    for (i = 0; i < (counts_send_row[rank] / board.cols_count); i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 1) {
                int value1 = local_row[i * board.cols_count + j];
                int value2 = local_row[i * board.cols_count + j + 1];

                if (value1 == value2) {
                    // Found a pair of values next to each other, mark all the other single values as black

                    for (k = 0; k < board.cols_count; k++) {
                        int single = local_row[i * board.cols_count + k];
                        bool isolated = true;
                        
                        if (k != j && k != j + 1 && single == value1) {

                            // Check if the value3 is isolated
                            if (k - 1 >= 0 && local_row[i * board.cols_count + k - 1] == single) isolated = false;
                            if (k + 1 < board.cols_count && local_row[i * board.cols_count + k + 1] == single) isolated = false;

                            if (isolated) {
                                local_row_solution[i * board.cols_count + k] = BLACK;

                                if (k - 1 >= 0) local_row_solution[i * board.cols_count + k - 1] = WHITE;
                                if (k + 1 < board.cols_count) local_row_solution[i * board.cols_count + k + 1] = WHITE;
                            }
                        }
                    }
                }
            }
        }
    }

    // For each local column, check if there are pairs of values
    for (i = 0; i < (counts_send_col[rank] / board.rows_count); i++) {
        for (j = 0; j < board.rows_count; j++) {
            if (j < board.rows_count - 1) {
                int value1 = local_col[i * board.rows_count + j];
                int value2 = local_col[i * board.rows_count + j + 1];

                if (value1 == value2) {
                    // Found a pair of values next to each other, mark all the other single values as black

                    for (k = 0; k < board.rows_count; k++) {
                        int single = local_col[i * board.rows_count + k];
                        bool isolated = true;
                        
                        if (k != j && k != j + 1 && single == value1) {

                            // Check if the value3 is isolated
                            if (k - 1 >= 0 && local_col[i * board.rows_count + k - 1] == single) isolated = false;
                            if (k + 1 < board.rows_count && local_col[i * board.rows_count + k + 1] == single) isolated = false;

                            if (isolated) {
                                local_col_solution[i * board.rows_count + k] = BLACK;

                                if (k - 1 >= 0) local_col_solution[i * board.rows_count + k - 1] = WHITE;
                                if (k + 1 < board.rows_count) local_col_solution[i * board.rows_count + k + 1] = WHITE;
                            }
                        }
                    }
                }
            }
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    struct Board solution = mpi_compute_and_share(board, rank, row_solution, col_solution, false, "Pair Isolation", true);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

struct Board mpi_flanked_isolation(struct Board board, int size, int rank) {

    /*
        RULE DESCRIPTION:
        
        If you find two doubles packed, singles must be black

        e.g. 2 3 3 2 ... 2 ... 3 --> 2 3 3 2 ... X ... X
    */
};

struct Board mpi_corner_cases(struct Board board, int size, int rank) {
    
    /*
        RULE DESCRIPTION:
        
        1) If you have a corner with three similar numbers, mark the other corner as black

        2) If you have a corner with two similar numbers and a single, mark the single as white
    */

    /*
        4 processes, each of which will receive 2 rows and two columns:
            1) first two rows and first two columns
            2) first two rows and last two columns
            3) last two rows and first two columns
            4) last two rows and last two columns
    */

    int *top_corner_solution, *bottom_corner_solution;

    MPI_Comm corner_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank < 4, rank, &corner_comm);

    if (rank < 4) {
        int *local_corner = (int *) malloc(2 * board.cols_count * sizeof(int));

        int x, y;

        switch (rank) {
            // Top corners
            case 0:
                x = 0;
                y = board.cols_count;
                memcpy(local_corner, board.grid, 2 * board.cols_count * sizeof(int));
                break;

            case 1:
                x = board.cols_count - 2;
                y = 2 * board.cols_count - 2;
                memcpy(local_corner, board.grid, 2 * board.cols_count * sizeof(int));
                break;

            // Bottom corners
            case 2:
                x = 0;
                y = board.cols_count;
                memcpy(local_corner, board.grid + (board.rows_count - 2) * board.cols_count, 2 * board.cols_count * sizeof(int));
                break;

            case 3:
                x = board.cols_count - 2;
                y = 2 * board.cols_count - 2;
                memcpy(local_corner, board.grid + (board.rows_count - 2) * board.cols_count, 2 * board.cols_count * sizeof(int));
                break;
        }

        int local_corner_solution[2 * board.cols_count];
        memset(local_corner_solution, UNKNOWN, 2 * board.cols_count * sizeof(int));

        int top_left = local_corner[x];
        int top_right = local_corner[x + 1];
        int bottom_left = local_corner[y];
        int bottom_right = local_corner[y + 1];

        // 1) Triple corner case
        switch (rank) {
            case 0:
                if (top_left == top_right && top_left == bottom_left) {
                    local_corner_solution[x] = BLACK;
                    local_corner_solution[x + 1] = WHITE;
                    local_corner_solution[y] = WHITE;

                } else if (bottom_right == top_right && bottom_right == bottom_left) {
                    local_corner_solution[y + 1] = BLACK;
                    local_corner_solution[y] = WHITE;
                    local_corner_solution[x + 1] = WHITE;
                }
                break;
            
            case 1:
                if (top_left == top_right && top_right == bottom_right) {
                    local_corner_solution[x + 1] = BLACK;
                    local_corner_solution[x] = WHITE;
                    local_corner_solution[y + 1] = WHITE;

                } else if (bottom_left == top_left && bottom_left == bottom_right) {
                    local_corner_solution[y] = BLACK;
                    local_corner_solution[x] = WHITE;
                    local_corner_solution[y + 1] = WHITE;
                }
                break;

            case 2:
                if (bottom_left == top_left && bottom_left == bottom_right) {
                    local_corner_solution[y] = BLACK;
                    local_corner_solution[x] = WHITE;
                    local_corner_solution[y + 1] = WHITE;

                } else if (top_right == top_left && top_right == bottom_right) {
                    local_corner_solution[x + 1] = BLACK;
                    local_corner_solution[x] = WHITE;
                    local_corner_solution[y + 1] = WHITE;
                }
                break;
            
            case 3:
                if (bottom_right == bottom_left && bottom_right == top_right) {
                    local_corner_solution[y + 1] = BLACK;
                    local_corner_solution[x + 1] = WHITE;
                    local_corner_solution[y] = WHITE;

                } else if (top_left == bottom_left && top_left == top_right) {
                    local_corner_solution[x] = BLACK;
                    local_corner_solution[x + 1] = WHITE;
                    local_corner_solution[y] = WHITE;
                }
                break;
        }

        // 2) Pair corner case
        switch (rank) {
            case 0:
                if (top_left == top_right) local_corner_solution[y] = WHITE;
                else if (top_left == bottom_left) local_corner_solution[x + 1] = WHITE;
                else if (bottom_left == bottom_right) local_corner_solution[x + 1] = WHITE;
                else if (top_right == bottom_right) local_corner_solution[y] = WHITE;
                break;
            case 1:
                if (top_left == top_right) local_corner_solution[y + 1] = WHITE;
                else if (top_right == bottom_right) local_corner_solution[x] = WHITE;
                else if (bottom_left == bottom_right) local_corner_solution[x] = WHITE;
                else if (top_left == bottom_left) local_corner_solution[y + 1] = WHITE;
                break;
            case 2:
                if (bottom_left == bottom_right) local_corner_solution[x] = WHITE;
                else if (top_left == bottom_left) local_corner_solution[y + 1] = WHITE;
                else if (top_left == top_right) local_corner_solution[y + 1] = WHITE;
                else if (top_right == bottom_right) local_corner_solution[x] = WHITE;
                break;
            case 3:
                if (bottom_right == bottom_left) local_corner_solution[x + 1] = WHITE;
                else if (top_right == bottom_right) local_corner_solution[y] = WHITE;
                else if (top_left == bottom_left) local_corner_solution[x + 1] = WHITE;
                else if (top_left == top_right) local_corner_solution[y] = WHITE;
                break;
        }

        int *corner_board_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
        memset(corner_board_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

        switch (rank) {
            case 0:
            case 1:
                memcpy(corner_board_solution, local_corner_solution, 2 * board.cols_count * sizeof(int));
                break;
            case 2:
            case 3:
                memcpy(corner_board_solution + (board.rows_count - 2) * board.cols_count, local_corner_solution, 2 * board.cols_count * sizeof(int));
                break;
        }

        int *solutions;

        if (rank == 0) {
            solutions = (int *) malloc(4 * board.rows_count * board.cols_count * sizeof(int));
        }

        MPI_Gather(corner_board_solution, board.rows_count * board.cols_count, MPI_INT, solutions, board.rows_count * board.cols_count, MPI_INT, 0, corner_comm);

        if (rank == 0) {
            int *top_left_board = solutions;
            int *top_right_board = solutions + board.rows_count * board.cols_count;
            int *bottom_left_board = solutions + 2 * board.rows_count * board.cols_count;
            int *bottom_right_board = solutions + 3 * board.rows_count * board.cols_count;

            struct Board top_corners = combine_partial_solutions(board, top_left_board, top_right_board, "Top Corner Cases", false, false);
            top_corner_solution = top_corners.solution;

            struct Board bottom_corners = combine_partial_solutions(board, bottom_left_board, bottom_right_board, "Bottom Corner Cases", false, false);
            bottom_corner_solution = bottom_corners.solution;
        }
    }

    MPI_Barrier(corner_comm);

    MPI_Comm_free(&corner_comm);

    MPI_Barrier(MPI_COMM_WORLD);

    return mpi_compute_and_share(board, rank, top_corner_solution, bottom_corner_solution, false, "Corner Cases", false);
}

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

    struct Board (*techniques[])(struct Board, int, int) = {
        mpi_uniqueness_rule,
        mpi_sandwich_rules,
        mpi_pair_isolation,
        mpi_corner_cases,
        mpi_set_white,
        mpi_set_black
    };

    int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

    int i;
    struct Board final_solution = techniques[0](board, size, rank);
    for (i = 1; i < num_techniques; i++) {
        final_solution = combine_board_solutions(final_solution, techniques[i](final_solution, size, rank), false);
        if (rank == 0) print_board("Partial", final_solution, SOLUTION);
    }

    while (true) {
        // mpi_set_white --> changed (bool)
        // mpi_set_black --> changed (bool)

        // MPI_Reduce --> [changed1, ....] 
    }

    // Bruteforce the final solution (backtrack)

    // Check validity of the final solution

    if (rank == 0) {
        write_solution(final_solution);

        if (DEBUG) print_board("Final", final_solution, SOLUTION);
    }

    MPI_Finalize();

    return 0;
}