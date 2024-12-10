#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define DEBUG false
#define INPUT_PATH "../test-cases/inputs/"
#define MAX_BUFFER_SIZE 2048
#define SOLUTION_SPACES 4
#define MANAGER_RANK 0

#define W2M_MESSAGE 0
#define M2W_MESSAGE 1
#define W2W_MESSAGE 2
#define W2W_BUFFER 3

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
    BCB items[SOLUTION_SPACES];
    int front;
    int rear;
} Queue;

typedef enum MessageType {
    TERMINATE = 1,                  // worker send termination signal to manager, which forwards it to other workers.
                                    // - data1: solver worker rank
    STATUS_UPDATE = 2,              // worker updates manager on its status (when changing queue size or when finishing).
                                    // - data1: queue size (0/-1 if worker is finished)
                                    // - data2: processes per solution space (0/-1 if worker is finished)
    ASK_FOR_WORK = 3,               // worker asks manager for more work, which in turn asks other worker to open a specific channel with said process.
    SEND_WORK = 4,                  // manager instructs worker to send work to another worker.
                                    // - data1: receiver rank
                                    // - data2: expected queue size of sender before sending work
    RECEIVE_WORK = 5,               // worker receives work from another worker.
                                    // - data1: sender rank
    FINISHED_SOLUTION_SPACE = 6,    // worker finished its solution space and is waiting for a new one. Manager sends finished message to ex master process.
                                    // - data1: worker id that finished
    
    // ======= DEDICATED MESSAGES =======

    WORKER_SEND_WORK = 7,           // worker sends work to another worker.
                                    // - data1: solutions to skip
                                    // - data2: total processes in solution space
    REFRESH_SOLUTION_SPACE = 8      // worker receives a new solution space and refreshes its solution space.
                                    // - data1: solutions to skip
                                    // - data2: total processes in solution space
} MessageType;

typedef struct Message {
    MessageType type;
    int data1;
    int data2;
    bool invalid;
} Message;

typedef struct WorkerStatus {
    int queue_size;
    int processes_sharing_solution_space;
    int master_process;
} WorkerStatus;

#endif