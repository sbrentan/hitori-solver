#include <mpi.h>
#include <stdlib.h>
#include <string.h>

#include "../include/pruning.h"
#include "../include/board.h"
#include "../include/utils.h"

Board mpi_uniqueness_rule(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {

    /*
        RULE DESCRIPTION:
        
        If a value is unique in a row or column, mark it as white.

        e.g. 2 3 2 1 1 --> 2 O 2 1 1
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, rank, size, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row, PRUNING_COMM);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, rank, size, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col, PRUNING_COMM);
    
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
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution, PRUNING_COMM);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution, PRUNING_COMM);

    Board row_board = (Board) { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = transpose((Board) { board.grid, board.rows_count, board.cols_count, col_solution });

    Board solution = combine_boards(row_board, col_board, true, rank, "Uniqueness Rule", PRUNING_COMM);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_sandwich_rules(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {

    /*
        RULE DESCRIPTION:
        
        1) Sandwich Triple: If you have three on a row, mark the edges as black and the middle as white

        e.g. 2 2 2 --> X O X
        
        2) Sandwich Pair: If you have two similar numbers with one between, you can mark the middle as white

        e.g. 2 3 2 --> 2 O 2
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, rank, size, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row, PRUNING_COMM);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, rank, size, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col, PRUNING_COMM);

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
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution, PRUNING_COMM);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution, PRUNING_COMM);

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = transpose((Board) { board.grid, board.rows_count, board.cols_count, col_solution });

    Board solution = combine_boards(row_board, col_board, false, rank, "Sandwich Rules", PRUNING_COMM);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_pair_isolation(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {

    /*
        RULE DESCRIPTION:
        
        If you have a double and some singles, you can mark all the singles as black

        e.g. 2 2 ... 2 ... 2 --> 2 2 ... X ... X
    */
    
    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, rank, size, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row, PRUNING_COMM);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, rank, size, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col, PRUNING_COMM);

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
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution, PRUNING_COMM);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution, PRUNING_COMM);

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = transpose((Board) { board.grid, board.rows_count, board.cols_count, col_solution });

    Board solution = combine_boards(row_board, col_board, false, rank, "Pair Isolation", PRUNING_COMM);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_flanked_isolation(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {

    /*
        RULE DESCRIPTION:
        
        If you find two doubles packed, singles must be black

        e.g. 2 3 3 2 ... 2 ... 3 --> 2 3 3 2 ... X ... X
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, rank, size, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row, PRUNING_COMM);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, rank, size, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col, PRUNING_COMM);

    int i, j, k;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    // For each local row, check if there is a flanked pair
    for (i = 0; i < (counts_send_row[rank] / board.cols_count); i++) {
        for (j = 0; j < board.cols_count; j++) {

            if (j < board.cols_count - 3) {
                int value1 = local_row[i * board.cols_count + j];
                int value2 = local_row[i * board.cols_count + j + 1];
                int value3 = local_row[i * board.cols_count + j + 2];
                int value4 = local_row[i * board.cols_count + j + 3];

                if (value1 == value4 && value2 == value3 && value1 != value2) {
                    for (k = 0; k < board.cols_count; k++) {
                        int single = local_row[i * board.cols_count + k];
                        if (k != j && k != j + 1 && k != j + 2 && k != j + 3 && (single == value1 || single == value2)) {
                            local_row_solution[i * board.cols_count + k] = BLACK;

                            if (k - 1 >= 0) local_row_solution[i * board.cols_count + k - 1] = WHITE;
                            if (k + 1 < board.cols_count) local_row_solution[i * board.cols_count + k + 1] = WHITE;
                        }
                    }
                }
            }
        }
    }

    // For each local column, check if there is a flanked pair
    for (i = 0; i < (counts_send_col[rank] / board.rows_count); i++) {
        for (j = 0; j < board.rows_count; j++) {

            if (j < board.rows_count - 3) {
                int value1 = local_col[i * board.rows_count + j];
                int value2 = local_col[i * board.rows_count + j + 1];
                int value3 = local_col[i * board.rows_count + j + 2];
                int value4 = local_col[i * board.rows_count + j + 3];

                if (value1 == value4 && value2 == value3 && value1 != value2) {
                    for (k = 0; k < board.rows_count; k++) {
                        int single = local_col[i * board.rows_count + k];
                        if (k != j && k != j + 1 && k != j + 2 && k != j + 3 && (single == value1 || single == value2)) {
                            local_col_solution[i * board.rows_count + k] = BLACK;

                            if (k - 1 >= 0) local_col_solution[i * board.rows_count + k - 1] = WHITE;
                            if (k + 1 < board.rows_count) local_col_solution[i * board.rows_count + k + 1] = WHITE;
                        }
                    }
                }
            }
        }
    }

    int *row_solution, *col_solution;
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution, PRUNING_COMM);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution, PRUNING_COMM);

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = transpose((Board) { board.grid, board.rows_count, board.cols_count, col_solution });

    Board solution = combine_boards(row_board, col_board, false, rank, "Flanked Isolation", PRUNING_COMM);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

void compute_corner(Board board, int x, int y, CornerType corner_type, int **local_corner_solution) {
    
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
        case BOTTOM_RIGHT:
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
        case BOTTOM_LEFT:
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
    }

    
    /*
        Pair Corner case: if you have a corner with two similar numbers, you can mark a single as white for all combinations
    */

    switch (corner_type) {
        case TOP_LEFT:
        case BOTTOM_RIGHT:
            if (top_left == top_right) (*local_corner_solution)[y] = WHITE;
            else if (top_left == bottom_left) (*local_corner_solution)[x + 1] = WHITE;
            else if (bottom_left == bottom_right) (*local_corner_solution)[x + 1] = WHITE;
            else if (top_right == bottom_right) (*local_corner_solution)[y] = WHITE;
            break;
        case TOP_RIGHT:
        case BOTTOM_LEFT:
            if (top_left == top_right) (*local_corner_solution)[y + 1] = WHITE;
            else if (top_right == bottom_right) (*local_corner_solution)[x] = WHITE;
            else if (bottom_left == bottom_right) (*local_corner_solution)[x] = WHITE;
            else if (top_left == bottom_left) (*local_corner_solution)[y + 1] = WHITE;
            break;
    }

    /*
        Quad Corner case: if you have a corner with four similar numbers, the diagonal cells must be black
    */
    
    if ((top_left == top_right && top_left == bottom_left && top_left == bottom_right) ||
        (top_right == bottom_right && top_left == bottom_left) ||
        (top_left == top_right && bottom_left == bottom_right)) {
            
            switch (corner_type) {        
                case TOP_LEFT:
                case BOTTOM_LEFT:
                    (*local_corner_solution)[x] = BLACK;
                    (*local_corner_solution)[x + 1] = WHITE;
                    (*local_corner_solution)[y] = WHITE;
                    (*local_corner_solution)[y + 1] = BLACK;
                    break;
                case TOP_RIGHT:
                case BOTTOM_RIGHT:
                    (*local_corner_solution)[x] = WHITE;
                    (*local_corner_solution)[x + 1] = BLACK;
                    (*local_corner_solution)[y] = BLACK;
                    (*local_corner_solution)[y + 1] = WHITE;
                    break;
            }
        }
    
    /*
        Corner Close: If you have a black in the corner, the other must be white
    */

    switch (corner_type) { 
        case TOP_LEFT:
        case BOTTOM_RIGHT:
            if (board.solution[x + 1] == BLACK) (*local_corner_solution)[y] = WHITE;
            else if (board.solution[y] == BLACK) (*local_corner_solution)[x + 1] = WHITE;
            break;
        case TOP_RIGHT:
        case BOTTOM_LEFT:
            if (board.solution[x] == BLACK) (*local_corner_solution)[y + 1] = WHITE;
            else if (board.solution[y + 1] == BLACK) (*local_corner_solution)[x] = WHITE;
            break;
    }
}

Board mpi_corner_cases(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {
    
    /*
        RULE DESCRIPTION:
        
        1) If you have a corner with three similar numbers, mark the angle cell as black

        2) If you have a corner with two similar numbers, you can mark a single as white for all combinations

        3) If you have a corner with four similar numbers, the diagonal cells must be black

        4) If you have a black in the corner, the other must be white
    */

    /*
        Compute the corner cases:
            1) If 1 process, compute all the corner cases
            2) If 2 or 3 processes, process 0 computes top corners and process 1 computes bottom corners (if 3, process 2 will be idle)
            3) If 4 processes or more, only four will compute a corner while the rest will be idle
    */

    /*
        If number of processes is greater than 1, use a different communication strategy:
            1) Two processes to compute the top and bottom corners (if 2 or 3 processes)
            2) Four processes to compute each corner (if 4 or more processes)
    */

    MPI_Comm TWO_PROCESSESS_COMM;
    int color = rank < 2 ? 1 : MPI_UNDEFINED;
    MPI_Comm_split(PRUNING_COMM, color, rank, &TWO_PROCESSESS_COMM);

    MPI_Comm FOUR_PROCESSESS_COMM;
    color = rank < 4 ? 1 : MPI_UNDEFINED;
    MPI_Comm_split(PRUNING_COMM, color, rank, &FOUR_PROCESSESS_COMM);
    
    int *solutions, *local_corner_solution;

    if (rank < 4) {

        /*
            If MANAGER, then allocate the memory for four boards, one for each corner
        */

        if (rank == MANAGER_RANK) solutions = (int *) malloc(4 * board.rows_count * board.cols_count * sizeof(int));

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
    }

    /*
        Combine the partial solutions
    */

    int rows = board.rows_count;
    int cols = board.cols_count;

    Board top_left_board = { board.grid, board.rows_count, board.cols_count, solutions };
    Board top_right_board = { board.grid, board.rows_count, board.cols_count, solutions + rows * cols };
    Board bottom_left_board = { board.grid, board.rows_count, board.cols_count, solutions + 2 * rows * cols };
    Board bottom_right_board = { board.grid, board.rows_count, board.cols_count, solutions + 3 * rows * cols };

    Board top_corners_board = combine_boards(top_left_board, top_right_board, false, rank, "Top Corner Cases", PRUNING_COMM);
    Board bottom_corners_board = combine_boards(bottom_left_board, bottom_right_board, false, rank, "Bottom Corner Cases", PRUNING_COMM);

    Board solution = combine_boards(top_corners_board, bottom_corners_board, false, rank, "Corner Cases", PRUNING_COMM);

    /*
        Destroy the temporary communicators, compute the final solution and broadcast it to all processes
    */

    if (TWO_PROCESSESS_COMM != MPI_COMM_NULL) MPI_Comm_free(&TWO_PROCESSESS_COMM);
    if (FOUR_PROCESSESS_COMM != MPI_COMM_NULL) MPI_Comm_free(&FOUR_PROCESSESS_COMM);

    return solution;
}

Board mpi_set_white(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {
    
    /*
        RULE DESCRIPTION:
        
        When you have whited a cell, you can mark all the other cells with the same number in the row or column as black
        
        e.g. Suppose a whited 3. Then 2 O ... 3 --> 2 O ... X
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, rank, size, ROWS, SOLUTION, &local_row, &counts_send_row, &displs_send_row, PRUNING_COMM);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, rank, size, COLS, SOLUTION, &local_col, &counts_send_col, &displs_send_col, PRUNING_COMM);

    int i, j, k;
    int local_row_solution[counts_send_row[rank]];
    int local_col_solution[counts_send_col[rank]];

    memset(local_row_solution, UNKNOWN, counts_send_row[rank] * sizeof(int));
    memset(local_col_solution, UNKNOWN, counts_send_col[rank] * sizeof(int));

    int starting_index = 0;

    for (i = 0; i < rank; i++)
        starting_index += counts_send_row[i];
    
    // For each local row, check if there is a white cell
    int rows_count = (counts_send_row[rank] / board.cols_count);
    for (i = 0; i < rows_count; i++) {
        int grid_row_index = starting_index + i * board.cols_count;
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

    Board TBoard = transpose(board);

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
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution, PRUNING_COMM);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution, PRUNING_COMM);

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution }; 
    Board col_board = transpose((Board) { board.grid, board.rows_count, board.cols_count, col_solution });

    Board solution = combine_boards(row_board, col_board, false, rank, "Set White", PRUNING_COMM);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_set_black(Board board, int rank, int size, MPI_Comm PRUNING_COMM) {
    
    /*
        RULE DESCRIPTION:
        
        When you have marked a black cell, all the cells around it must be white.

        e.g. 2 X 2 --> O X O
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, rank, size, ROWS, SOLUTION, &local_row, &counts_send_row, &displs_send_row, PRUNING_COMM);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, rank, size, COLS, SOLUTION, &local_col, &counts_send_col, &displs_send_col, PRUNING_COMM);

    int i, j;
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
    mpi_gather_board(board, rank, local_row_solution, counts_send_row, displs_send_row, &row_solution, PRUNING_COMM);
    mpi_gather_board(board, rank, local_col_solution, counts_send_col, displs_send_col, &col_solution, PRUNING_COMM);

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = transpose((Board) { board.grid, board.rows_count, board.cols_count, col_solution });

    Board solution = combine_boards(row_board, col_board, false, rank, "Set Black", PRUNING_COMM);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}
