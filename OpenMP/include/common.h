#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define DEBUG 0
#define INPUT_PATH "../test-cases/inputs/"
#define MAX_BUFFER_SIZE 2048
#define SOLUTION_SPACES 8

// Definition of the cell states for the hitori board
typedef enum CellState {
    UNKNOWN = -1,
    WHITE = 0,
    BLACK = 1
} CellState;

// Definition of the board types, BOARD is the original board, SOLUTION is the board with the solution
typedef enum BoardType {
    BOARD = 0,
    SOLUTION = 1
} BoardType;

// Definition of the scatter type, ROWS is used to scatter the rows of the board, COLS is used to scatter the columns of the board
typedef enum ScatterType {
    ROWS = 0,
    COLS = 1
} ScatterType;

// Definition of the corner types for the pruning of the corner cases
typedef enum CornerType {
    TOP_LEFT = 0,
    TOP_RIGHT = 1,
    BOTTOM_LEFT = 2,
    BOTTOM_RIGHT = 3
} CornerType;  

// Definition of the board structure
typedef struct Board {
    int *grid;
    int rows_count;
    int cols_count;
    CellState *solution;
} Board;

// Board Control Block
typedef struct BCB {
    CellState *solution;            // This matrix contains the solution for the block
    bool *solution_space_unknowns;  // This matrix defines for each unknown if it has been marked as a cell state in the solution space definition
} BCB;

// Definition of the circular queue structure 
typedef struct Queue {
    BCB *items;
    int front;
    int rear;
    int size;
} Queue;


#endif