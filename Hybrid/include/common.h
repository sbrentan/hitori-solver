#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define DEBUG 0                                 // Debug flag
#define INPUT_PATH "../test-cases/inputs/"      // Path to the input files
#define MAX_BUFFER_SIZE 2048                    // Maximum buffer size for reading the input file
#define SOLUTION_SPACES 8                       // Number of solution spaces
#define MANAGER_RANK 0                          // Rank of the manager process
#define MANAGER_THREAD 0                        // Manager thread of a process
#define MAX_MSG_SIZE 10                         

// MPI_Messages tags definition
#define W2M_MESSAGE 0                           // Message from worker to manager
#define M2W_MESSAGE 1                           // Message from manager to worker
#define W2W_MESSAGE 2                           // Message from worker to worker
#define W2W_BUFFER 3                            // Buffer from worker to worker

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

// Definition of the message structure
typedef struct Message {
    MessageType type;               // Type of the message
    int data1;                      // First parameter of the message [optional]
    int data2;                      // Second parameter of the message [optional]
    bool invalid;                   // Flag to indicate if the message is invalid
} Message;

// Definition of the worker status structure
typedef struct WorkerStatus {
    int queue_size;                                 // Identifies the number of elments (blocks) in the queue to be processed 
    int processes_sharing_solution_space;           // Identifies the number of processes that are sharing the same solution space
    int master_process;                             // Identifies the master process of the solution space
} WorkerStatus;

#endif