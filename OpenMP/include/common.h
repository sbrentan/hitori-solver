#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define DEBUG 0
#define INPUT_PATH "../test-cases/inputs/"
#define MAX_BUFFER_SIZE 2048
#define SOLUTION_SPACES 8
#define LEAF_QUEUE_SIZE 20

typedef enum CellState {
    UNKNOWN = -1,
    WHITE = 0,
    BLACK = 1
} CellState;

typedef enum BoardType {
    BOARD = 0,
    SOLUTION = 1
} BoardType;

typedef enum ScatterType {
    ROWS = 0,
    COLS = 1
} ScatterType;

typedef enum CornerType {
    TOP_LEFT = 0,
    TOP_RIGHT = 1,
    BOTTOM_LEFT = 2,
    BOTTOM_RIGHT = 3
} CornerType;  

typedef struct Board {
    int *grid;
    int rows_count;
    int cols_count;
    CellState *solution;
} Board;

// Board Control Block
typedef struct BCB {
    CellState *solution;
    bool *solution_space_unknowns;  // This matrix defines for each unknown if it has been marked as a cell state in the solution space definition
} BCB;

typedef struct Queue {
    BCB *items;
    int front;
    int rear;
    int size;
} Queue;


#endif