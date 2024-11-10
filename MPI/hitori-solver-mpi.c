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

enum CornerType {
    TOP_LEFT = 0,
    TOP_RIGHT = 1,
    BOTTOM_LEFT = 2,
    BOTTOM_RIGHT = 3
};  

typedef struct Board {
    int *grid;
    int rows_count;
    int cols_count;
    int *solution;
} Board;

/* ------------------ GENERAL HELPERS ------------------ */

void read_board(int **board, int *rows_count, int *cols_count, int **solution) {

    /*
        Helper function to read the board from the input file.
    */
    
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

void write_solution(struct Board board) {

    /*
        Helper function to write the solution to the output file.
    */

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

void print_board(char *title, struct Board board, enum Type type) {

    /*
        Helper function to print the board.
    */
    
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

/* ------------------ BOARD HELPERS ------------------ */

void pack_board(struct Board board, int **buffer, int *buffer_size) {

    /*
        Helper function to pack the board into a buffer.
    */

    int pos = 0;
    int size;
    
    int int_size, grid_size;
    MPI_Pack_size(1, MPI_INT, MPI_COMM_WORLD, &int_size);
    MPI_Pack_size(board.rows_count * board.cols_count, MPI_INT, MPI_COMM_WORLD, &grid_size);
    size = 2 * int_size + 2 * grid_size;
    
    *buffer = (int *) malloc(size);

    MPI_Pack(&board.rows_count, 1, MPI_INT, *buffer, size, &pos, MPI_COMM_WORLD);
    MPI_Pack(&board.cols_count, 1, MPI_INT, *buffer, size, &pos, MPI_COMM_WORLD);
    MPI_Pack(board.grid, board.rows_count * board.cols_count, MPI_INT, *buffer, size, &pos, MPI_COMM_WORLD);
    MPI_Pack(board.solution, board.rows_count * board.cols_count, MPI_INT, *buffer, size, &pos, MPI_COMM_WORLD);

    *buffer_size = size;
}

void unpack_board(int *buffer, int buffer_size, struct Board *board) {
    
    /*
        Helper function to unpack the board from a buffer.
    */

    int position = 0;

    MPI_Unpack(buffer, buffer_size, &position, &board->rows_count, 1, MPI_INT, MPI_COMM_WORLD);
    MPI_Unpack(buffer, buffer_size, &position, &board->cols_count, 1, MPI_INT, MPI_COMM_WORLD);

    board->grid = (int *) malloc(board->rows_count * board->cols_count * sizeof(int));
    board->solution = (int *) malloc(board->rows_count * board->cols_count * sizeof(int));

    MPI_Unpack(buffer, buffer_size, &position, board->grid, board->rows_count * board->cols_count, MPI_INT, MPI_COMM_WORLD);
    MPI_Unpack(buffer, buffer_size, &position, board->solution, board->rows_count * board->cols_count, MPI_INT, MPI_COMM_WORLD);
}

struct Board transpose(struct Board board) {

    /*
        Helper function to transpose a matrix.
    */

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

    /*
        Combine the solutions of two boards to get the final solution.
    */

    struct Board final;
    final.grid = first_board.grid;
    final.rows_count = first_board.rows_count;
    final.cols_count = first_board.cols_count;
    final.solution = (int *) malloc(final.rows_count * final.cols_count * sizeof(int));
    memset(final.solution, UNKNOWN, final.rows_count * final.cols_count * sizeof(int));

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

struct Board combine_partial_solutions(struct Board board, int *row_solution, int *col_solution, char *technique, bool forced, bool transpose_cols) {

    /*
        Combine the partial solutions from the rows and columns to get the final solution.
    */

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

/* ------------------ MPI UTILS ------------------ */

void mpi_share_board(struct Board board, int rank, struct Board *local_board) {

    /*
        Share the board with all the processes.
    */
    
    int *buffer, buffer_size;

    if (rank == 0) pack_board(board, &buffer, &buffer_size);

    MPI_Bcast(&buffer_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank != 0) buffer = (int *) malloc(buffer_size);

    MPI_Bcast(buffer, buffer_size, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank != 0) unpack_board(buffer, buffer_size, local_board);

    free(buffer);
}

void mpi_scatter_board(struct Board board, int size, int rank, enum ScatterType scatter_type, enum Type target_type, int **local_vector, int **counts_send, int **displs_send) {

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
        Scatter the board, generating a local vector for each process based on the target_type.
        If the target_type is BOARD, scatter the board, otherwise scatter the solution.
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
    
    /*
        Initialize the solution board with the values of the original board.
    */

    struct Board solution;
    solution.grid = board.grid;
    solution.rows_count = board.rows_count;
    solution.cols_count = board.cols_count;
    solution.solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

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

    int starting_index = 0;

    for (i = 0; i < rank; i++) {
        starting_index += counts_send_row[i];
    }
    
    // For each local row, check if there is a white cell
    int rows_count = (counts_send_row[rank] / board.cols_count);
    if(rank == 0) printf("rows_count: %d\n", rows_count);
    for (i = 0; i < rows_count; i++) {
        int grid_row_index = starting_index + i * board.cols_count;
        if(rank == 0) printf("index: %d\n", grid_row_index);
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

    struct Board TBoard = transpose(board);

    // For each local column, check if triplet values are present
    int cols_count = (counts_send_col[rank] / board.rows_count);
    for (i = 0; i < cols_count; i++) {
        int grid_col_index = starting_index + i * board.rows_count;
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

void compute_corner(struct Board board, int x, int y, enum CornerType corner_type, int **local_corner_solution) {
    
    /*
        Set the local board to work with the corner
    */

    *local_corner_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    memset(*local_corner_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    /*
        Get the values of the corner cells based on the corner indexes (x, y):
        1) x --> top left 
        2) x + 1 --> top right
        3) y --> bottom left
        4) y + 1 --> bottom right
    */

    int top_left = board.grid[x];
    int top_right = board.grid[x + 1];
    int bottom_left = board.grid[y];
    int bottom_right = board.grid[y + 1];
    
    /*
        Triple Corner case: if you have a corner with three similar numbers, mark the angle cell as black
    */

    switch (corner_type) {
        case TOP_LEFT:
            if (top_left == top_right && top_left == bottom_left) {
                (*local_corner_solution)[x] = BLACK;
                (*local_corner_solution)[x + 1] = WHITE;
                (*local_corner_solution)[y] = WHITE;

            } else if (bottom_right == top_right && bottom_right == bottom_left) {
                (*local_corner_solution)[y + 1] = BLACK;
                (*local_corner_solution)[y] = WHITE;
                (*local_corner_solution)[x + 1] = WHITE;
            }
            break;
        
        case TOP_RIGHT:
            if (top_left == top_right && top_right == bottom_right) {
                (*local_corner_solution)[x + 1] = BLACK;
                (*local_corner_solution)[x] = WHITE;
                (*local_corner_solution)[y + 1] = WHITE;

            } else if (bottom_left == top_left && bottom_left == bottom_right) {
                (*local_corner_solution)[y] = BLACK;
                (*local_corner_solution)[x] = WHITE;
                (*local_corner_solution)[y + 1] = WHITE;
            }
            break;

        case BOTTOM_LEFT:
            if (bottom_left == top_left && bottom_left == bottom_right) {
                (*local_corner_solution)[y] = BLACK;
                (*local_corner_solution)[x] = WHITE;
                (*local_corner_solution)[y + 1] = WHITE;

            } else if (top_right == top_left && top_right == bottom_right) {
                (*local_corner_solution)[x + 1] = BLACK;
                (*local_corner_solution)[x] = WHITE;
                (*local_corner_solution)[y + 1] = WHITE;
            }
            break;
        
        case BOTTOM_RIGHT:
            if (bottom_right == bottom_left && bottom_right == top_right) {
                (*local_corner_solution)[y + 1] = BLACK;
                (*local_corner_solution)[x + 1] = WHITE;
                (*local_corner_solution)[y] = WHITE;

            } else if (top_left == bottom_left && top_left == top_right) {
                (*local_corner_solution)[x] = BLACK;
                (*local_corner_solution)[x + 1] = WHITE;
                (*local_corner_solution)[y] = WHITE;
            }
            break;
    }

    
    /*
        Pair Corner case: if you have a corner with two similar numbers, you can mark a single as white for all combinations
    */

    switch (corner_type) {
        case TOP_LEFT:
            if (top_left == top_right) (*local_corner_solution)[y] = WHITE;
            else if (top_left == bottom_left) (*local_corner_solution)[x + 1] = WHITE;
            else if (bottom_left == bottom_right) (*local_corner_solution)[x + 1] = WHITE;
            else if (top_right == bottom_right) (*local_corner_solution)[y] = WHITE;
            break;
        case TOP_RIGHT:
            if (top_left == top_right) (*local_corner_solution)[y + 1] = WHITE;
            else if (top_right == bottom_right) (*local_corner_solution)[x] = WHITE;
            else if (bottom_left == bottom_right) (*local_corner_solution)[x] = WHITE;
            else if (top_left == bottom_left) (*local_corner_solution)[y + 1] = WHITE;
            break;
        case BOTTOM_LEFT:
            if (bottom_left == bottom_right) (*local_corner_solution)[x] = WHITE;
            else if (top_left == bottom_left) (*local_corner_solution)[y + 1] = WHITE;
            else if (top_left == top_right) (*local_corner_solution)[y + 1] = WHITE;
            else if (top_right == bottom_right) (*local_corner_solution)[x] = WHITE;
            break;
        case BOTTOM_RIGHT:
            if (bottom_right == bottom_left) (*local_corner_solution)[x + 1] = WHITE;
            else if (top_right == bottom_right) (*local_corner_solution)[y] = WHITE;
            else if (top_left == bottom_left) (*local_corner_solution)[x + 1] = WHITE;
            else if (top_left == top_right) (*local_corner_solution)[y] = WHITE;
            break;
    }
}

struct Board mpi_corner_cases(struct Board board, int size, int rank) {
    
    /*
        RULE DESCRIPTION:
        
        1) If you have a corner with three similar numbers, mark the angle cell as black

        2) If you have a corner with two similar numbers, you can mark a single as white for all combinations
    */

    /*
        Compute the corner cases:
            1) If 1 process, compute all the corner cases
            2) If 2 or 3 processes, process 0 computes top corners and process 1 computes bottom corners (if 3, process 2 will be idle)
            3) If 4 processes or more, each process will compute a corner while the rest will be idle
    */

    int *top_corners_solution, *bottom_corners_solution;

    /*
        If number of processes is greater than 1, use a different communication strategy:
            1) Two processes to compute the top and bottom corners (if 2 or 3 processes)
            2) Four processes to compute each corner (if 4 or more processes)
    */

    MPI_Comm TWO_PROCESSESS_COMM;
    MPI_Comm_split(MPI_COMM_WORLD, rank < 2, rank, &TWO_PROCESSESS_COMM);

    MPI_Comm FOUR_PROCESSESS_COMM;
    MPI_Comm_split(MPI_COMM_WORLD, rank < 4, rank, &FOUR_PROCESSESS_COMM);

    if (rank < 4) {

        int *solutions, *local_corner_solution;

        /*
            If process 0, then allocate the memory for four boards, one for each corner
        */

        if (rank == 0) solutions = (int *) malloc(4 * board.rows_count * board.cols_count * sizeof(int));

        /*
            Define all the indexes for each corner
        */

        int top_left_x = 0;
        int top_left_y = board.cols_count;

        int top_right_x = board.cols_count - 2;
        int top_right_y = 2 * board.cols_count - 2;

        int bottom_left_x = (board.rows_count - 2) * board.cols_count;
        int bottom_left_y = (board.rows_count - 1) * board.cols_count;

        int bottom_right_x = (board.rows_count - 2) * board.cols_count + board.cols_count - 2;
        int bottom_right_y = (board.rows_count - 1) * board.cols_count + board.cols_count - 2;

        /*
            Compute the corner cases and communicate the results to process 0
        */

        switch (size) {
            case 1:
                compute_corner(board, top_left_x, top_left_y, TOP_LEFT, &local_corner_solution);
                memcpy(solutions, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                compute_corner(board, top_right_x, top_right_y, TOP_RIGHT, &local_corner_solution);
                memcpy(solutions + board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                compute_corner(board, bottom_left_x, bottom_left_y, BOTTOM_LEFT, &local_corner_solution);
                memcpy(solutions + 2 * board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                compute_corner(board, bottom_right_x, bottom_right_y, BOTTOM_RIGHT, &local_corner_solution);
                memcpy(solutions + 3 * board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                break;
            case 2:
            case 3:                
                if (rank == 0) {
                    compute_corner(board, top_left_x, top_left_y, TOP_LEFT, &local_corner_solution);
                    memcpy(solutions, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                    MPI_Recv(solutions + 2 * board.rows_count * board.cols_count, board.rows_count * board.cols_count, MPI_INT, 1, 0, TWO_PROCESSESS_COMM, MPI_STATUS_IGNORE);

                    compute_corner(board, top_right_x, top_right_y, TOP_RIGHT, &local_corner_solution);
                    memcpy(solutions + board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                    MPI_Recv(solutions + 3 * board.rows_count * board.cols_count, board.rows_count * board.cols_count, MPI_INT, 1, 0, TWO_PROCESSESS_COMM, MPI_STATUS_IGNORE);
                    
                } else if (rank == 1) {
                    compute_corner(board, bottom_left_x, bottom_left_y, BOTTOM_LEFT, &local_corner_solution);
                    MPI_Send(local_corner_solution, board.rows_count * board.cols_count, MPI_INT, 0, 0, TWO_PROCESSESS_COMM);
                    
                    compute_corner(board, bottom_right_x, bottom_right_y, BOTTOM_RIGHT, &local_corner_solution);
                    MPI_Send(local_corner_solution, board.rows_count * board.cols_count, MPI_INT, 0, 0, TWO_PROCESSESS_COMM);
                }
                break;
            default:
                if (rank == 0) compute_corner(board, top_left_x, top_left_y, TOP_LEFT, &local_corner_solution);
                else if (rank == 1) compute_corner(board, top_right_x, top_right_y, TOP_RIGHT, &local_corner_solution);
                else if (rank == 2) compute_corner(board, bottom_left_x, bottom_left_y, BOTTOM_LEFT, &local_corner_solution);
                else if (rank == 3) compute_corner(board, bottom_right_x, bottom_right_y, BOTTOM_RIGHT, &local_corner_solution);
                
                MPI_Gather(local_corner_solution, board.rows_count * board.cols_count, MPI_INT, solutions + rank * board.rows_count * board.cols_count, board.rows_count * board.cols_count, MPI_INT, 0, FOUR_PROCESSESS_COMM);
                break;
            
            free(local_corner_solution);
        }

        /*
            Combine the partial solutions
        */

        if (rank == 0) {

            int *top_left_solution = solutions;
            int *top_right_solution = solutions + board.rows_count * board.cols_count;
            int *bottom_left_solution = solutions + 2 * board.rows_count * board.cols_count;
            int *bottom_right_solution = solutions + 3 * board.rows_count * board.cols_count;

            struct Board top_corners_board = combine_partial_solutions(board, top_left_solution, top_right_solution, "Top Corner Cases", false, false);
            top_corners_solution = top_corners_board.solution;

            struct Board bottom_corners_board = combine_partial_solutions(board, bottom_left_solution, bottom_right_solution, "Bottom Corner Cases", false, false);
            bottom_corners_solution = bottom_corners_board.solution;

            free_memory((int *[]){solutions, top_left_solution, top_right_solution, bottom_left_solution, bottom_right_solution});
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    /*
        Destroy the temporary communicators, compute the final solution and broadcast it to all processes
    */

    MPI_Comm_free(&TWO_PROCESSESS_COMM);
    MPI_Comm_free(&FOUR_PROCESSESS_COMM);

    return mpi_compute_and_share(board, rank, top_corners_solution, bottom_corners_solution, false, "Corner Cases", false);
}

struct Board mpi_flanked_isolation(struct Board board, int size, int rank) {

    /*
        RULE DESCRIPTION:
        
        If you find two doubles packed, singles must be black

        e.g. 2 3 3 2 ... 2 ... 3 --> 2 3 3 2 ... X ... X
    */
};

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

    mpi_share_board(board, rank, &board);

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

    // Bruteforce the final solution (backtrack)

    // Check validity of the final solution

    if (rank == 0) {
        write_solution(final_solution);

        if (DEBUG) print_board("Final", final_solution, SOLUTION);
    }

    free_memory((int *[]){board.grid, board.solution, final_solution.grid, final_solution.solution});

    MPI_Finalize();

    return 0;
}