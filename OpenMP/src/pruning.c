#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../include/pruning.h"
#include "../include/board.h"

Board uniqueness_rule(Board board) {

    /*
        RULE DESCRIPTION:
        
        If a value is unique in a row or column, mark it as white.

        e.g. 2 3 2 1 1 --> 2 O 2 1 1
    */

    int i, j, k;
    bool unique;
    
    int *uniqueness_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    memset(uniqueness_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {

            int current_value = board.grid[i * board.cols_count + j];

            unique = true;
            for (k = 0; k < board.cols_count; k++) {
                if (j != k && current_value == board.grid[i * board.cols_count + k]) {
                    unique = false;
                    break;
                }
            }

            if (unique) {
                for (k = 0; k < board.rows_count; k++) {
                    if (i != k && current_value == board.grid[k * board.rows_count + j]) {
                        unique = false;
                        break;
                    }
                }

                if (unique)
                    uniqueness_solution[i * board.cols_count + j] = WHITE; // If the value is unique, mark it as white
            }
        }
    }

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };
    memcpy(solution.solution, uniqueness_solution, board.rows_count * board.cols_count * sizeof(int));

    return solution;
}

Board sandwich_rules(Board board) {

    /*
        RULE DESCRIPTION:
        
        1) Sandwich Triple: If you have three on a row, mark the edges as black and the middle as white

        e.g. 2 2 2 --> X O X
        
        2) Sandwich Pair: If you have two similar numbers with one between, you can mark the middle as white

        e.g. 2 3 2 --> 2 O 2
    */

    int i, j;

    int *sandwich_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    memset(sandwich_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 2) {

                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[i * board.cols_count + j + 1];
                int value3 = board.grid[i * board.cols_count + j + 2];

                if (value1 == value2 && value2 == value3) {
                    sandwich_solution[i * board.cols_count + j] = BLACK;
                    sandwich_solution[i * board.cols_count + j + 1] = WHITE;
                    sandwich_solution[i * board.cols_count + j + 2] = BLACK;

                    if (j - 1 >= 0) sandwich_solution[i * board.cols_count + j - 1] = WHITE;
                    if (j + 3 < board.cols_count) sandwich_solution[i * board.cols_count + j + 3] = WHITE;

                } else if (value1 != value2 && value1 == value3) {
                    sandwich_solution[i * board.cols_count + j + 1] = WHITE;
                }
            }

            if (i < board.rows_count - 2) {

                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[(i + 1) * board.cols_count + j];
                int value3 = board.grid[(i + 2) * board.cols_count + j];

                if (value1 == value2 && value2 == value3) {
                    sandwich_solution[i * board.cols_count + j] = BLACK;
                    sandwich_solution[(i + 1) * board.cols_count + j] = WHITE;
                    sandwich_solution[(i + 2) * board.cols_count + j] = BLACK;

                    if (i - 1 >= 0) sandwich_solution[(i - 1) * board.cols_count + j] = WHITE;
                    if (i + 3 < board.rows_count) sandwich_solution[(i + 3) * board.cols_count + j] = WHITE;

                } else if (value1 != value2 && value1 == value3) {
                    sandwich_solution[(i + 1) * board.cols_count + j] = WHITE;
                }
            }
        }
    }

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };
    memcpy(solution.solution, sandwich_solution, board.rows_count * board.cols_count * sizeof(int));

    return solution;
}

Board pair_isolation(Board board) {

    /*
        RULE DESCRIPTION:
        
        If you have a double and some singles, you can mark all the singles as black

        e.g. 2 2 ... 2 ... 2 --> 2 2 ... X ... X
    */

    int i, j, k;

    int *pair_isolation_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    memset(pair_isolation_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 1) {
                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[i * board.cols_count + j + 1];

                if (value1 == value2) {
                    // Found a pair of values next to each other, mark all the other single values as black
                    for (k = 0; k < board.cols_count; k++) {
                        int single = board.grid[i * board.cols_count + k];
                        bool isolated = true;

                        if (k != j && k != j + 1 && single == value1) {

                            // Check if the value is isolated
                            if (k - 1 >= 0 && board.grid[i * board.cols_count + k - 1] == single) isolated = false;
                            if (k + 1 < board.cols_count && board.grid[i * board.cols_count + k + 1] == single) isolated = false;

                            if (isolated) {
                                pair_isolation_solution[i * board.cols_count + k] = BLACK;

                                if (k - 1 >= 0) pair_isolation_solution[i * board.cols_count + k - 1] = WHITE;
                                if (k + 1 < board.cols_count) pair_isolation_solution[i * board.cols_count + k + 1] = WHITE;
                            }
                        }
                    }
                }
            }
            
            if (i < board.rows_count - 1) {
                int value1 = board.grid[i * board.rows_count + j];
                int value2 = board.grid[(i + 1) * board.rows_count + j];

                if (value1 == value2) {
                    // Found a pair of values next to each other, mark all the other single values as black
                    for (k = 0; k < board.rows_count; k++) {
                        int single = board.grid[k * board.rows_count + j];
                        bool isolated = true;

                        if (k != i && k != i + 1 && single == value1) {

                            // Check if the value is isolated
                            if (k - 1 >= 0 && board.grid[(k - 1) * board.rows_count + j] == single) isolated = false;
                            if (k + 1 < board.rows_count && board.grid[(k + 1) * board.rows_count + j] == single) isolated = false;

                            if (isolated) {
                                pair_isolation_solution[k * board.rows_count + j] = BLACK;

                                if (k - 1 >= 0) pair_isolation_solution[(k - 1) * board.rows_count + j] = WHITE;
                                if (k + 1 < board.rows_count) pair_isolation_solution[(k + 1) * board.rows_count + j] = WHITE;
                            }
                        }
                    }
                }
            }
        }
    }

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };
    memcpy(solution.solution, pair_isolation_solution, board.rows_count * board.cols_count * sizeof(int));

    return solution;

}

Board flanked_isolation(Board board) {

    /*
        RULE DESCRIPTION:
        
        If you find two doubles packed, singles must be black

        e.g. 2 3 3 2 ... 2 ... 3 --> 2 3 3 2 ... X ... X
    */

    int i, j, k;

    int *flanked_isolation_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    memset(flanked_isolation_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));
    
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (j < board.cols_count - 3) {
                int value1 = board.grid[i * board.cols_count + j];
                int value2 = board.grid[i * board.cols_count + j + 1];
                int value3 = board.grid[i * board.cols_count + j + 2];
                int value4 = board.grid[i * board.cols_count + j + 3];

                if (value1 == value4 && value2 == value3 && value1 != value2) {
                    for (k = 0; k < board.cols_count; k++) {
                        int single = board.grid[i * board.cols_count + k];
                        if (k != j && k != j + 1 && k != j + 2 && k != j + 3 && (single == value1 || single == value2)) {
                            flanked_isolation_solution[i * board.cols_count + k] = BLACK;

                            if (k - 1 >= 0) flanked_isolation_solution[i * board.cols_count + k - 1] = WHITE;
                            if (k + 1 < board.cols_count) flanked_isolation_solution[i * board.cols_count + k + 1] = WHITE;
                        }
                    }
                }
            }
            
            if (i < board.rows_count - 3) {
                int value1 = board.grid[i * board.rows_count + j];
                int value2 = board.grid[(i + 1) * board.rows_count + j];
                int value3 = board.grid[(i + 2) * board.rows_count + j];
                int value4 = board.grid[(i + 3) * board.rows_count + j];

                if (value1 == value4 && value2 == value3 && value1 != value2) {
                    for (k = 0; k < board.rows_count; k++) {
                        int single = board.grid[k * board.rows_count + j];
                        if (k != i && k != i + 1 && k != i + 2 && k != i + 3 && (single == value1 || single == value2)) {
                            flanked_isolation_solution[k * board.rows_count + j] = BLACK;

                            if (k - 1 >= 0) flanked_isolation_solution[(k - 1) * board.rows_count + j] = WHITE;
                            if (k + 1 < board.rows_count) flanked_isolation_solution[(k + 1) * board.rows_count + j] = WHITE;
                        }
                    }
                }
            }
        }
    }

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };
    memcpy(solution.solution, flanked_isolation_solution, board.rows_count * board.cols_count * sizeof(int));

    return solution;
}

void compute_corner(Board board, int x, int y, CornerType corner_type, int **solution) {

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
                (*solution)[x] = BLACK;
                (*solution)[x + 1] = WHITE;
                (*solution)[y] = WHITE;

            } else if (bottom_right == top_right && bottom_right == bottom_left) {
                (*solution)[y + 1] = BLACK;
                (*solution)[y] = WHITE;
                (*solution)[x + 1] = WHITE;
            }
            break;
        
        case TOP_RIGHT:
        case BOTTOM_LEFT:
            if (top_left == top_right && top_right == bottom_right) {
                (*solution)[x + 1] = BLACK;
                (*solution)[x] = WHITE;
                (*solution)[y + 1] = WHITE;

            } else if (bottom_left == top_left && bottom_left == bottom_right) {
                (*solution)[y] = BLACK;
                (*solution)[x] = WHITE;
                (*solution)[y + 1] = WHITE;
            }
            break;
    }

    
    /*
        Pair Corner case: if you have a corner with two similar numbers, you can mark a single as white for all combinations
    */

    switch (corner_type) {
        case TOP_LEFT:
        case BOTTOM_RIGHT:
            if (top_left == top_right) (*solution)[y] = WHITE;
            else if (top_left == bottom_left) (*solution)[x + 1] = WHITE;
            else if (bottom_left == bottom_right) (*solution)[x + 1] = WHITE;
            else if (top_right == bottom_right) (*solution)[y] = WHITE;
            break;
        case TOP_RIGHT:
        case BOTTOM_LEFT:
            if (top_left == top_right) (*solution)[y + 1] = WHITE;
            else if (top_right == bottom_right) (*solution)[x] = WHITE;
            else if (bottom_left == bottom_right) (*solution)[x] = WHITE;
            else if (top_left == bottom_left) (*solution)[y + 1] = WHITE;
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
                    (*solution)[x] = BLACK;
                    (*solution)[x + 1] = WHITE;
                    (*solution)[y] = WHITE;
                    (*solution)[y + 1] = BLACK;
                    break;
                case TOP_RIGHT:
                case BOTTOM_RIGHT:
                    (*solution)[x] = WHITE;
                    (*solution)[x + 1] = BLACK;
                    (*solution)[y] = BLACK;
                    (*solution)[y + 1] = WHITE;
                    break;
            }
        }
    
    /*
        Corner Close: If you have a black in the corner, the other must be white
    */

    switch (corner_type) { 
        case TOP_LEFT:
        case BOTTOM_RIGHT:
            if (board.solution[x + 1] == BLACK) (*solution)[y] = WHITE;
            else if (board.solution[y] == BLACK) (*solution)[x + 1] = WHITE;
            break;
        case TOP_RIGHT:
        case BOTTOM_LEFT:
            if (board.solution[x] == BLACK) (*solution)[y + 1] = WHITE;
            else if (board.solution[y + 1] == BLACK) (*solution)[x] = WHITE;
            break;
    }
}

Board corner_cases(Board board) {  
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

    int rows = board.rows_count;
    int cols = board.cols_count;
    int board_size = rows * cols;

    int top_left_x = 0;
    int top_left_y = cols;

    int top_right_x = cols - 2;
    int top_right_y = 2 * cols - 2;

    int bottom_left_x = (rows - 2) * cols;
    int bottom_left_y = (rows - 1) * cols;

    int bottom_right_x = (rows - 2) * cols + cols - 2;
    int bottom_right_y = (rows - 1) * cols + cols - 2;

    int *corner_solution = (int *) malloc(board_size * sizeof(int));
    memset(corner_solution, UNKNOWN, board_size * sizeof(int));

    compute_corner(board, top_left_x, top_left_y, TOP_LEFT, &corner_solution);
    compute_corner(board, top_right_x, top_right_y, TOP_RIGHT, &corner_solution);
    compute_corner(board, bottom_left_x, bottom_left_y, BOTTOM_LEFT, &corner_solution);
    compute_corner(board, bottom_right_x, bottom_right_y, BOTTOM_RIGHT, &corner_solution);

    /*
        Initialize the board with the corner solution
    */

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board_size * sizeof(int)) };
    memcpy(solution.solution, corner_solution, board_size * sizeof(int));

    free(corner_solution);

    return solution;
}

Board set_white(Board board) {
    
    /*
        RULE DESCRIPTION:
        
        When you have whited a cell, you can mark all the other cells with the same number in the row or column as black
        
        e.g. Suppose a whited 3. Then 2 O ... 3 --> 2 O ... X
    */

    int i, j, k;

    int *set_white_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));    
    memset(set_white_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == WHITE) {
                int value = board.grid[i * board.cols_count + j];
                
                for (k = 0; k < board.cols_count; k++) {
                    if (board.grid[i * board.cols_count + k] == value && k != j) {
                        set_white_solution[i * board.cols_count + k] = BLACK;

                        if (k - 1 >= 0) set_white_solution[i * board.cols_count + k - 1] = WHITE;
                        if (k + 1 < board.cols_count) set_white_solution[i * board.cols_count + k + 1] = WHITE;
                    }
                }

                for (k = 0; k < board.rows_count; k++) {
                    if (board.grid[k * board.rows_count + j] == value && k != i) {
                        set_white_solution[k * board.rows_count + j] = BLACK;

                        if (k - 1 >= 0) set_white_solution[(k - 1) * board.rows_count + j] = WHITE;
                        if (k + 1 < board.rows_count) set_white_solution[(k + 1) * board.rows_count + j] = WHITE;
                    }
                }
            }
        }
    }

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };
    memcpy(solution.solution, set_white_solution, board.rows_count * board.cols_count * sizeof(int));

    return solution;
}

Board set_black(Board board) {
    
    /*
        RULE DESCRIPTION:
        
        When you have marked a black cell, all the cells around it must be white.

        e.g. 2 X 2 --> O X O
    */

    int i, j;

    int *set_black_solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    memset(set_black_solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(int));

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == BLACK) {
                if (j - 1 >= 0) set_black_solution[i * board.cols_count + j - 1] = WHITE;
                if (j + 1 < board.cols_count) set_black_solution[i * board.cols_count + j + 1] = WHITE;
                
                if (i - 1 >= 0) set_black_solution[(i - 1) * board.rows_count + j ] = WHITE;
                if (i + 1 < board.rows_count) set_black_solution[(i + 1) * board.rows_count + j ] = WHITE;
            }
        }
    }

    Board solution = { board.grid, board.rows_count, board.cols_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };
    memcpy(solution.solution, set_black_solution, board.rows_count * board.cols_count * sizeof(int));

    return solution;
}
