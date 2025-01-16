#include <stdlib.h>
#include <string.h>

#include "../include/pruning.h"
#include "../include/board.h"

Board openmp_uniqueness_rule(Board board) {

    /*
        RULE DESCRIPTION:
        
        If a value is unique in a row or column, mark it as white.

        e.g. 2 3 2 1 1 --> 2 O 2 1 1
    */

    int i, j, k;
    
    int row_solution[board.rows_count * board.cols_count];
    int col_solution[board.rows_count * board.cols_count];

    memset(row_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    memset(col_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    #pragma omp parallel for private(i, j, k) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            bool unique = true;
            int value = board.grid[i * board.cols_count + j];

            for (k = 0; k < board.cols_count; k++) {
                if (j != k && value == board.grid[i * board.cols_count + k]) {
                    unique = false;
                    break;
                }
            }

            if (unique)
                row_solution[i * board.cols_count + j] = WHITE; // If the value is unique, mark it as white
            else
                row_solution[i * board.cols_count + j] = UNKNOWN;
        }
    }

    #pragma omp parallel for private(i, j, k) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {// j = 1 i = 0
        for (j = 0; j < board.cols_count; j++) {
            bool unique = true;
            int value = board.grid[j * board.rows_count + i];

            for (k = 0; k < board.rows_count; k++) {
                if (j != k && value == board.grid[k * board.rows_count + i]) {
                    unique = false;
                    break;
                }
            }

            if (unique)
                col_solution[j * board.rows_count + i] = WHITE; // If the value is unique, mark it as white
            else
                col_solution[j * board.rows_count + i] = UNKNOWN;
        }
    }


    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = { board.grid, board.rows_count, board.cols_count, col_solution };

    Board solution = combine_boards(row_board, col_board, true, "Uniqueness Rule");

    return solution;
}

Board openmp_sandwich_rules(Board board) {

    /*
        RULE DESCRIPTION:
        
        1) Sandwich Triple: If you have three on a row, mark the edges as black and the middle as white

        e.g. 2 2 2 --> X O X
        
        2) Sandwich Pair: If you have two similar numbers with one between, you can mark the middle as white

        e.g. 2 3 2 --> 2 O 2
    */

    int i, j;

    int row_solution[board.rows_count * board.cols_count];
    int col_solution[board.rows_count * board.cols_count];

    memset(row_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    memset(col_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 2) {
                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[i * board.cols_count + j + 1];
                int value3 = board.grid[i * board.cols_count + j + 2];

                if (value1 == value2 && value2 == value3) {
                    row_solution[i * board.cols_count + j] = BLACK;
                    row_solution[i * board.cols_count + j + 1] = WHITE;
                    row_solution[i * board.cols_count + j + 2] = BLACK;

                    if (j - 1 >= 0) row_solution[i * board.cols_count + j - 1] = WHITE;
                    if (j + 3 < board.cols_count) row_solution[i * board.cols_count + j + 3] = WHITE;

                } else if (value1 != value2 && value1 == value3) {
                    row_solution[i * board.cols_count + j] = UNKNOWN;
                    row_solution[i * board.cols_count + j + 1] = WHITE;
                    row_solution[i * board.cols_count + j + 2] = UNKNOWN;
                }
            }
        }
    }

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (i < board.rows_count - 2) {
                int value1 = board.grid[j * board.rows_count + i];
                int value2 = board.grid[(j+1)* board.rows_count + i];
                int value3 = board.grid[(j+2) * board.rows_count + i];

                if (value1 == value2 && value2 == value3) {
                    col_solution[j * board.rows_count + i] = BLACK;
                    col_solution[(j+1) * board.rows_count + i] = WHITE;
                    col_solution[(j+2) * board.rows_count + i] = BLACK;

                    if (j - 1 >= 0) col_solution[(j-1) * board.rows_count + i] = WHITE;
                    if (j + 3 < board.rows_count) col_solution[(j+3) * board.rows_count + i] = WHITE;

                } else if (value1 != value2 && value1 == value3) {
                    col_solution[j * board.rows_count + i] = UNKNOWN;
                    col_solution[(j+1) * board.rows_count + i] = WHITE;
                    col_solution[(j+2) * board.rows_count + i] = UNKNOWN;
                }
            }
        }
    }

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = { board.grid, board.rows_count, board.cols_count, col_solution };

    Board solution = combine_boards(row_board, col_board, false, "Sandwich Rules");

    return solution;
}

Board openmp_pair_isolation(Board board) {

    /*
        RULE DESCRIPTION:
        
        If you have a double and some singles, you can mark all the singles as black

        e.g. 2 2 ... 2 ... 2 --> 2 2 ... X ... X
    */

    int i, j;

    int row_solution[board.rows_count * board.cols_count];
    int col_solution[board.rows_count * board.cols_count];

    memset(row_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    memset(col_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 1) {
                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[i * board.cols_count + j + 1];

                if (value1 == value2) {
                    // Found a pair of values next to each other, mark all the other single values as black
                    int k;
                    for (k = 0; k < board.cols_count; k++) {
                        int single = board.grid[i * board.cols_count + k];
                        bool isolated = true;

                        if (k != j && k != j + 1 && single == value1) {

                            // Check if the value is isolated
                            if (k - 1 >= 0 && board.grid[i * board.cols_count + k - 1] == single) isolated = false;
                            if (k + 1 < board.cols_count && board.grid[i * board.cols_count + k + 1] == single) isolated = false;

                            if (isolated) {
                                row_solution[i * board.cols_count + k] = BLACK;

                                if (k - 1 >= 0) row_solution[i * board.cols_count + k - 1] = WHITE;
                                if (k + 1 < board.cols_count) row_solution[i * board.cols_count + k + 1] = WHITE;
                            }
                        }
                    }
                }
            }
        }
    }

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (i < board.rows_count - 1) {
                int value1 = board.grid[j * board.rows_count + i];
                int value2 = board.grid[(j+1) * board.rows_count + i];

                if (value1 == value2) {
                    // Found a pair of values next to each other, mark all the other single values as black
                    int k;
                    for (k = 0; k < board.rows_count; k++) {
                        int single = board.grid[k * board.rows_count + i];
                        bool isolated = true;

                        if (k != j && k != j + 1 && single == value1) {

                            // Check if the value is isolated
                            if (k - 1 >= 0 && board.grid[(k-1) * board.rows_count + i] == single) isolated = false;
                            if (k + 1 < board.rows_count && board.grid[(k+1) * board.rows_count + i] == single) isolated = false;

                            if (isolated) {
                                col_solution[k * board.rows_count + i] = BLACK;

                                if (k - 1 >= 0) col_solution[(k-1) * board.rows_count + i] = WHITE;
                                if (k + 1 < board.rows_count) col_solution[(k+1) * board.rows_count + i] = WHITE;
                            }
                        }
                    }
                }
            }
        }
    }

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = { board.grid, board.rows_count, board.cols_count, col_solution };

    Board solution = combine_boards(row_board, col_board, false, "Pair Isolation");

    return solution;

}

Board openmp_flanked_isolation(Board board) {

    /*
        RULE DESCRIPTION:
        
        If you find two doubles packed, singles must be black

        e.g. 2 3 3 2 ... 2 ... 3 --> 2 3 3 2 ... X ... X
    */

    int i, j;

    int row_solution[board.rows_count * board.cols_count];
    int col_solution[board.rows_count * board.cols_count];

    memset(row_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    memset(col_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    
    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 3) {
                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[i * board.cols_count + j + 1];
                int value3 = board.grid[i * board.cols_count + j + 2];
                int value4 = board.grid[i * board.cols_count + j + 3];

                if (value1 == value4 && value2 == value3 && value1 != value2) {
                    int k;
                    for (k = 0; k < board.cols_count; k++) {
                        int single = board.grid[i * board.cols_count + k];
                        if (k != j && k != j + 1 && k != j + 2 && k != j + 3 && (single == value1 || single == value2)) {
                            row_solution[i * board.cols_count + k] = BLACK;

                            if (k - 1 >= 0) row_solution[i * board.cols_count + k - 1] = WHITE;
                            if (k + 1 < board.cols_count) row_solution[i * board.cols_count + k + 1] = WHITE;
                        }
                    }
                }
            }
        }
    }

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (i < board.rows_count - 3) {
                int value1 = board.grid[j * board.rows_count + i];
                int value2 = board.grid[(j+1) * board.rows_count + i];
                int value3 = board.grid[(j+2) * board.rows_count + i];
                int value4 = board.grid[(j+3) * board.rows_count + i];

                if (value1 == value4 && value2 == value3 && value1 != value2) {
                    int k;
                    for (k = 0; k < board.rows_count; k++) {
                        int single = board.grid[k * board.rows_count + i];
                        if (k != j && k != j + 1 && k != j + 2 && k != j + 3 && (single == value1 || single == value2)) {
                            col_solution[k * board.rows_count + i] = BLACK;

                            if (k - 1 >= 0) col_solution[(k-1) * board.rows_count + i] = WHITE;
                            if (k + 1 < board.rows_count) col_solution[(k+1) * board.rows_count + i] = WHITE;
                        }
                    }
                }
            }
        }
    }

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = { board.grid, board.rows_count, board.cols_count, col_solution };

    Board solution = combine_boards(row_board, col_board, false, "Flanked Isolation");

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

Board openmp_corner_cases(Board board) {  
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
            3) If 4 processes or more, each process will compute a corner while the rest will be idle
    */

    /*
        If number of processes is greater than 1, use a different communication strategy:
            1) Two processes to compute the top and bottom corners (if 2 or 3 processes)
            2) Four processes to compute each corner (if 4 or more processes)
    */

    int *local_corner_solution;
    int *solutions = (int *) malloc(4 * board.rows_count * board.cols_count * sizeof(int));

    #pragma omp parallel
    {   
        int size = omp_get_num_threads();
        int rank = omp_get_thread_num();

        int top_left_x = 0;
        int top_left_y = board.cols_count;

        int top_right_x = board.cols_count - 2;
        int top_right_y = 2 * board.cols_count - 2;

        int bottom_left_x = (board.rows_count - 2) * board.cols_count;
        int bottom_left_y = (board.rows_count - 1) * board.cols_count;

        int bottom_right_x = (board.rows_count - 2) * board.cols_count + board.cols_count - 2;
        int bottom_right_y = (board.rows_count - 1) * board.cols_count + board.cols_count - 2;
        
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
                    compute_corner(board, top_right_x, top_right_y, TOP_RIGHT, &local_corner_solution);
                    memcpy(solutions + board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                } else if (rank == 1) {
                    compute_corner(board, bottom_left_x, bottom_left_y, BOTTOM_LEFT, &local_corner_solution);
                    memcpy(solutions + 2 * board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                    compute_corner(board, bottom_right_x, bottom_right_y, BOTTOM_RIGHT, &local_corner_solution);
                    memcpy(solutions + 3 * board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                }
                break;
            default:
                switch (rank) {
                    case 0:
                        compute_corner(board, top_left_x, top_left_y, TOP_LEFT, &local_corner_solution);
                        memcpy(solutions, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                        break;
                    case 1:
                        compute_corner(board, top_right_x, top_right_y, TOP_RIGHT, &local_corner_solution);
                        memcpy(solutions + board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                        break;
                    case 2:
                        compute_corner(board, bottom_left_x, bottom_left_y, BOTTOM_LEFT, &local_corner_solution);
                        memcpy(solutions + 2 * board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                        break;
                    case 3:
                        compute_corner(board, bottom_right_x, bottom_right_y, BOTTOM_RIGHT, &local_corner_solution);
                        memcpy(solutions + 3 * board.rows_count * board.cols_count, local_corner_solution, board.rows_count * board.cols_count * sizeof(int));
                        break;
                }
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

    Board top_corners_board = combine_boards(top_left_board, top_right_board, false, "Top Corner Cases");
    Board bottom_corners_board = combine_boards(bottom_left_board, bottom_right_board, false, "Bottom Corner Cases");

    Board solution = combine_boards(top_corners_board, bottom_corners_board, false, "Corner Cases");

    free(solutions);

    return solution;
}

Board openmp_set_white(Board board) {
    
    /*
        RULE DESCRIPTION:
        
        When you have whited a cell, you can mark all the other cells with the same number in the row or column as black
        
        e.g. Suppose a whited 3. Then 2 O ... 3 --> 2 O ... X
    */

    int i, j;

    int row_solution[board.rows_count * board.cols_count];
    int col_solution[board.rows_count * board.cols_count];

    memset(row_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    memset(col_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == WHITE) {
                int value = board.grid[i * board.cols_count + j];
                int k;
                for (k = 0; k < board.cols_count; k++) {
                    if (board.grid[i * board.cols_count + k] == value && k != j) {
                        row_solution[i * board.cols_count + k] = BLACK;

                        if (k - 1 >= 0) row_solution[i * board.cols_count + k - 1] = WHITE;
                        if (k + 1 < board.cols_count) row_solution[i * board.cols_count + k + 1] = WHITE;
                    }
                }
            }
        }
    }

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[j * board.rows_count + i] == WHITE) {
                int value = board.grid[j * board.rows_count + i];
                int k;
                for (k = 0; k < board.rows_count; k++) {
                    if (board.grid[k * board.rows_count + i] == value && k != j) {
                        col_solution[k * board.rows_count + i] = BLACK;

                        if (k - 1 >= 0) col_solution[(k-1) * board.rows_count + i] = WHITE;
                        if (k + 1 < board.rows_count) col_solution[(k+1) * board.rows_count + i] = WHITE;
                    }
                }
            }
        }
    }

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = { board.grid, board.rows_count, board.cols_count, col_solution };

    Board solution = combine_boards(row_board, col_board, false, "Set White");

    return solution;
}

Board openmp_set_black(Board board) {
    
    /*
        RULE DESCRIPTION:
        
        When you have marked a black cell, all the cells around it must be white.

        e.g. 2 X 2 --> O X O
    */

    int i, j;

    int row_solution[board.rows_count * board.cols_count];
    int col_solution[board.rows_count * board.cols_count];

    memset(row_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    memset(col_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == BLACK) {
                if (j - 1 >= 0) row_solution[i * board.cols_count + j - 1] = WHITE;
                if (j + 1 < board.cols_count) row_solution[i * board.cols_count + j + 1] = WHITE;
            }
        }
    }

    #pragma omp parallel for private(i, j) collapse(2) schedule(static)
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[j * board.rows_count + i] == BLACK) {
                if (j - 1 >= 0) col_solution[(j-1) * board.rows_count + i ] = WHITE;
                if (j + 1 < board.rows_count) col_solution[(j+1) * board.rows_count + i ] = WHITE;
            }
        }
    }

    Board row_board = { board.grid, board.rows_count, board.cols_count, row_solution };
    Board col_board = { board.grid, board.rows_count, board.cols_count, col_solution };

    Board solution = combine_boards(row_board, col_board, false, "Set Black");

    return solution;
}
