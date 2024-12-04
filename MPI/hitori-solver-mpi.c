#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>

#define DEBUG false
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

typedef struct {
    BCB items[SOLUTION_SPACES];
    int front;
    int rear;
} Queue;

int rank, size, solver_process = -1;
double ctime = 0;
Board board;
Queue solution_queue;
int *unknown_index, *unknown_index_length;
bool terminated = false;

int solutions_to_skip = 0;
int total_processes_in_solution_space = 1;
int *processes_in_my_solution_space;

bool is_my_solution_spaces_ended = false;



/* ------------------ MESSAGE PASSING VARIABLES ------------------ */

typedef enum MessageType {
    TERMINATE = 1,                  // worker send termination signal to manager, which forwards it to other workers.
                                    // - data1: solver worker rank
    STATUS_UPDATE = 2,              // worker updates manager on its status (when changing queue size or when finishing).
                                    // - data1: queue size (0/-1 if worker is finished)
                                    // - data2: processes per solution space (0/-1 if worker is finished)
    ASK_FOR_WORK = 3,               // worker asks manager for more work, which in turn asks other worker to open a specific channel with said process.
    SEND_WORK = 4,                  // manager instructs worker to send work to another worker.
                                    // - data1: receiver rank
    RECEIVE_WORK = 5,               // worker receives work from another worker.
                                    // - data1: sender rank
    
    // ======= DEDICATED MESSAGES =======

    WORKER_SEND_WORK = 6,           // worker sends work to another worker.
                                    // - data1: solutions to skip
                                    // - data2: total processes in solution space
    REFRESH_SOLUTION_SPACE = 7,     // worker receives a new solution space and refreshes its solution space.
                                    // - data1: solutions to skip
                                    // - data2: total processes in solution space
    FINISHED_SOLUTION_SPACE = 8     // worker finished its solution space and is waiting for a new one. Manager sends finished message to ex master process.
                                    // - data1: worker id that finished

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

// ----- Common variables -----
MPI_Datatype MPI_MESSAGE;

// ----- Worker variables -----
Message manager_message, receive_work_message, refresh_solution_space_message, finished_solution_space_message;
MPI_Request manager_request;   // Request for the workers to contact the manager
MPI_Request receive_work_request, refresh_solution_space_request, finished_solution_space_request; // dedicated worker-worker
int *receive_work_buffer, *send_work_buffer;


// ----- Manager variables -----
Message *worker_messages;
MPI_Request *worker_requests;  // Requests for the manager to contact the workers
int *rank_to_request_mapping;
WorkerStatus *worker_statuses;


// TODO: is_my_solution_spaces_ended not correct anymore

// when a process is manager of more solution spaces, and it concedes one of them. Who is the manager of the new transferred solution space?
// Solution space worker managers are no more
//  - At the beginning, as now, all now about the division of initial solution spaces (also manager)
//    If other processes ask for sharing it will redirect them to the right process (if it hasnt finished building leaf they will wait)


/* ------------------ FUNCTION DECLARATIONS ------------------ */

void worker_wait_for_message(MPI_Request *request, Queue *queue);


/* ------------------ GENERAL HELPERS ------------------ */

void read_board(int **board, int *rows_count, int *cols_count, CellState **solution) {

    /*
        Helper function to read the board from the input file.
    */
    
    FILE *fp = fopen("../test-cases/inputs/input-15x15.txt", "r");
    
    if (fp == NULL) {
        printf("Could not open file.\n");
        exit(1);
    }

    printf("Reading board...\n");

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

    printf("Rows: %d\n", rows);
    printf("Cols: %d\n", cols);

    if (rows != cols) {
        printf("The board must be a square.\n");
        exit(1);
    }

    *board = (int *) malloc(rows * cols * sizeof(int));
    *solution = (int *) malloc(rows * cols * sizeof(CellState));

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

void write_solution(Board board) {

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

void print_board(char *title, Board board, BoardType type) {

    /*
        Helper function to print the board.
    */
    
    char buffer[MAX_BUFFER_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "# --- %s --- #\n", title);

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (type == BOARD) 
                snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d ", board.grid[i * board.cols_count + j]);
            else
                if (board.solution[i * board.cols_count + j] == WHITE) 
                    strncat(buffer, "O ", sizeof(buffer) - strlen(buffer) - 1);
                else if (board.solution[i * board.cols_count + j] == BLACK) 
                    strncat(buffer, "X ", sizeof(buffer) - strlen(buffer) - 1);
                else 
                    strncat(buffer, "? ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }
    strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    printf("%s", buffer);
}

void print_block(char *title, BCB* block) {
    
    /*
        Helper function to print the block.
    */

    char buffer[MAX_BUFFER_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "# --- %s --- #\n", title);
    
    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (block->solution[i * board.cols_count + j] == WHITE) 
                strncat(buffer, "O ", sizeof(buffer) - strlen(buffer) - 1);
            else if (block->solution[i * board.cols_count + j] == BLACK) 
                strncat(buffer, "X ", sizeof(buffer) - strlen(buffer) - 1);
            else 
                strncat(buffer, "? ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    strncat(buffer, "\n --- Unknowns --- \n", sizeof(buffer) - strlen(buffer) - 1);

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            char unknown[3];
            snprintf(unknown, sizeof(unknown), "%d ", block->solution_space_unknowns[i * board.cols_count + j]);
            strncat(buffer, unknown, sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    printf("%s", buffer);
    
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

Board transpose(Board board) {

    /*
        Helper function to transpose a matrix.
    */

    Board Tboard;
    Tboard.rows_count = board.cols_count;
    Tboard.cols_count = board.rows_count;
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

Board deep_copy(Board board){
    Board copy = board;
    copy.grid = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    copy.solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            copy.grid[i * board.cols_count + j] = board.grid[i * board.cols_count + j];
            copy.solution[i * board.cols_count + j] = board.solution[i * board.cols_count + j];
        }
    }

    return copy;
}

bool is_board_equal(Board first_board, Board second_board, BoardType type) {

    /*
        Helper function to check if two boards are equal.
    */

    if (first_board.rows_count != second_board.rows_count || first_board.cols_count != second_board.cols_count) return false;

    int *first = (type == BOARD) ? first_board.grid : first_board.solution;
    int *second = (type == BOARD) ? second_board.grid : second_board.solution;

    int i, j;
    for (i = 0; i < first_board.rows_count; i++)
        for (j = 0; j < first_board.cols_count; j++)
            if (first[i * first_board.cols_count + j] != second[i * first_board.cols_count + j]) return false;

    return true;
}

Board combine_board_solutions(Board first_board, Board second_board, bool forced) {

    /*
        Combine the solutions of two boards to get the final solution.
    */

    Board final;
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

Board combine_partial_solutions(Board board, int *row_solution, int *col_solution, char *technique, bool forced, bool transpose_cols) {

    /*
        Combine the partial solutions from the rows and columns to get the final solution.
    */

    Board row_board = board;
    row_board.solution = row_solution;

    Board col_board = board;
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

Board mpi_compute_and_share(Board board, int *row_solution, int *col_solution, bool forced, char *technique, bool transpose_cols) {
    
    /*
        Initialize the solution board with the values of the original board.
    */

    Board solution;
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

/* ------------------ QUEUE HELPERS ------------------ */

// Function to initialize the queue
void initializeQueue(Queue* q) {
    q->front = -1;
    q->rear = -1;
}

int isFull(Queue* q) {
    // If the next position is the front, the queue is full
    return (q->rear + 1) % SOLUTION_SPACES == q->front;
}

int getQueueSize(Queue* q) {
    // If the queue is empty, return 0
    if (q->front == -1) return 0;
    // If the front is behind the rear, return the difference
    if (q->front <= q->rear) return q->rear - q->front + 1;
    // If the front is ahead of the rear, return the difference plus the size of the queue
    return SOLUTION_SPACES - q->front + q->rear + 1;
} 

// Function to check if the queue is empty
bool isEmpty(Queue* q) {
    return q->front == -1;
}

void enqueue(Queue *q, BCB *block) {
    // If the queue is full, print an error message and
    // return
    if (isFull(q)) {
        printf("Queue overflow\n");
        return;
    }
    // If the queue is empty, set the front to the first
    // position
    if (q->front == -1) {
        q->front = 0;
    }
    // Add the data to the queue and move the rear pointer
    q->rear = (q->rear + 1) % SOLUTION_SPACES;
    q->items[q->rear] = *block;
    //printf("Element %lld inserted\n", value);
}

BCB peek(Queue* q) {
    // If the queue is empty, print an error message and
    // return -1
    if (isEmpty(q)) {
        printf("Queue underflow\n");
        exit(-1);
    }
    // Return the front element
    return q->items[q->front];
}

BCB dequeue(Queue* q) {
    // If the queue is empty, print an error message and
    // return -1
    if (isEmpty(q)) {
        printf("Queue underflow\n");
        exit(-1);
    }
    // Get the data from the front of the queue
    BCB data = q->items[q->front];
    // If the front and rear pointers are at the same
    // position, reset them
    if (q->front == q->rear) {
        q->front = q->rear = -1;
    }
    else {
        // Otherwise, move the front pointer to the next
        // position
        q->front = (q->front + 1) % SOLUTION_SPACES;
    }
    // Return the dequeued data
    return data;
}

void printQueue(Queue* q) {
    printf("Skipping printing queue");
    return;
    // // If the queue is empty, print a message and return
    // if (isEmpty(q)) {
    //     printf("Queue is empty\n");
    //     return;
    // }
    // // Print the elements in the queue
    // printf("Queue elements: ");
    // int i = q->front;
    // while (i != q->rear) {
    //     printf("%d ", q->items[i]);
    //     i = (i + 1) % SOLUTION_SPACES;
    // }
    // // Print the last element
    // printf("%d\n", q->items[q->rear]);
}
/* ------------------ MPI UTILS ------------------ */

void mpi_share_board() {

    /*
        Share the board with all the processes.
    */

    MPI_Bcast(&board.rows_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&board.cols_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (rank != 0) {
        board.grid = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
        board.solution = (CellState *) malloc(board.rows_count * board.cols_count * sizeof(CellState));
    }

    MPI_Bcast(board.grid, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board.solution, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
}

void mpi_scatter_board(Board board, ScatterType scatter_type, BoardType target_type, int **local_vector, int **counts_send, int **displs_send) {

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

    int i, offset = 0;

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

void mpi_gather_board(Board board, int *local_vector, int *counts_send, int *displs_send, int **solution) {

    /*
        Gather the local vectors from each process and combine them to get the final solution.
    */

    *solution = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    MPI_Gatherv(local_vector, counts_send[rank], MPI_INT, *solution, counts_send, displs_send, MPI_INT, 0, MPI_COMM_WORLD);
}

/* ------------------ HITORI PRUNING TECNIQUES ------------------ */

Board mpi_uniqueness_rule(Board board) {

    /*
        RULE DESCRIPTION:
        
        If a value is unique in a row or column, mark it as white.

        e.g. 2 3 2 1 1 --> 2 O 2 1 1
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);
    
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
    mpi_gather_board(board, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    Board solution = mpi_compute_and_share(board, row_solution, col_solution, true, "Uniqueness Rule", true);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_set_white(Board board) {
    
    /*
        RULE DESCRIPTION:
        
        When you have whited a cell, you can mark all the other cells with the same number in the row or column as black
        
        e.g. Suppose a whited 3. Then 2 O ... 3 --> 2 O ... X
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, ROWS, SOLUTION, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, COLS, SOLUTION, &local_col, &counts_send_col, &displs_send_col);

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
    mpi_gather_board(board, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    Board solution = mpi_compute_and_share(board, row_solution, col_solution, false, "Set White", true);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_set_black(Board board) {
    
    /*
        RULE DESCRIPTION:
        
        When you have marked a black cell, all the cells around it must be white.

        e.g. 2 X 2 --> O X O
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, ROWS, SOLUTION, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, COLS, SOLUTION, &local_col, &counts_send_col, &displs_send_col);

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
    mpi_gather_board(board, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    Board solution = mpi_compute_and_share(board, row_solution, col_solution, false, "Set Black", true);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_sandwich_rules(Board board) {

    /*
        RULE DESCRIPTION:
        
        1) Sandwich Triple: If you have three on a row, mark the edges as black and the middle as white

        e.g. 2 2 2 --> X O X
        
        2) Sandwich Pair: If you have two similar numbers with one between, you can mark the middle as white

        e.g. 2 3 2 --> 2 O 2
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);

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
    mpi_gather_board(board, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    Board solution = mpi_compute_and_share(board, row_solution, col_solution, false, "Sandwich Rules", true);
    
    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

Board mpi_pair_isolation(Board board) {

    /*
        RULE DESCRIPTION:
        
        If you have a double and some singles, you can mark all the singles as black

        e.g. 2 2 ... 2 ... 2 --> 2 2 ... X ... X
    */
    
    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);

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
    mpi_gather_board(board, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    Board solution = mpi_compute_and_share(board, row_solution, col_solution, false, "Pair Isolation", true);
    
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

Board mpi_corner_cases(Board board) {
    
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

            Board top_corners_board = combine_partial_solutions(board, top_left_solution, top_right_solution, "Top Corner Cases", false, false);
            top_corners_solution = top_corners_board.solution;

            Board bottom_corners_board = combine_partial_solutions(board, bottom_left_solution, bottom_right_solution, "Bottom Corner Cases", false, false);
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

    return mpi_compute_and_share(board, top_corners_solution, bottom_corners_solution, false, "Corner Cases", false);
}

Board mpi_flanked_isolation(Board board) {

    /*
        RULE DESCRIPTION:
        
        If you find two doubles packed, singles must be black

        e.g. 2 3 3 2 ... 2 ... 3 --> 2 3 3 2 ... X ... X
    */

    int *local_row, *counts_send_row, *displs_send_row;
    mpi_scatter_board(board, ROWS, BOARD, &local_row, &counts_send_row, &displs_send_row);

    int *local_col, *counts_send_col, *displs_send_col;
    mpi_scatter_board(board, COLS, BOARD, &local_col, &counts_send_col, &displs_send_col);

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
    mpi_gather_board(board, local_row_solution, counts_send_row, displs_send_row, &row_solution);
    mpi_gather_board(board, local_col_solution, counts_send_col, displs_send_col, &col_solution);

    Board solution = mpi_compute_and_share(board, row_solution, col_solution, false, "Flanked Isolation", true);

    free_memory((int *[]){local_row, counts_send_row, displs_send_row, local_col, counts_send_col, displs_send_col, row_solution, col_solution});

    return solution;
}

/* ------------------ SOLUTION 5 VALIDATION ------------------ */

bool is_cell_state_valid(BCB* block, int x, int y, CellState cell_state) {
    if (cell_state == BLACK) {
        if (x > 0 && block->solution[(x - 1) * board.cols_count + y] == BLACK) return false;
        if (x < board.rows_count - 1 && block->solution[(x + 1) * board.cols_count + y] == BLACK) return false;
        if (y > 0 && block->solution[x * board.cols_count + y - 1] == BLACK) return false;
        if (y < board.cols_count - 1 && block->solution[x * board.cols_count + y + 1] == BLACK) return false;
    } else if (cell_state == WHITE) {
        int i, j, cell_value = board.grid[x * board.cols_count + y];
        // TODO: optimize this (if rows=columns) or use a sum table
        for (i = 0; i < board.rows_count; i++)
            if (i != x && board.grid[i * board.cols_count + y] == cell_value && block->solution[i * board.cols_count + y] == WHITE)
                return false;
        for (j = 0; j < board.cols_count; j++)
            if (j != y && board.grid[x * board.cols_count + j] == cell_value && block->solution[x * board.cols_count + j] == WHITE)
                return false;
    }
    return true;
} 

int dfs_white_cells(BCB *block, bool* visited, int row, int col) {
    if (row < 0 || row >= board.rows_count || col < 0 || col >= board.cols_count) return 0;
    if (visited[row * board.cols_count + col]) return 0;
    if (block->solution[row * board.cols_count + col] == BLACK) return 0;

    visited[row * board.cols_count + col] = true;

    int count = 1;
    count += dfs_white_cells(block, visited, row - 1, col);
    count += dfs_white_cells(block, visited, row + 1, col);
    count += dfs_white_cells(block, visited, row, col - 1);
    count += dfs_white_cells(block, visited, row, col + 1);
    return count;
}

bool all_white_cells_connected(BCB* block) {

    bool *visited = malloc((board.rows_count * board.cols_count) * sizeof(bool));
    memset(visited, false, board.rows_count * board.cols_count * sizeof(bool));

    // Count all the white cells, and find the first white cell
    int i, j;
    int row = -1, col = -1;
    int white_cells_count = 0;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (block->solution[i * board.cols_count + j] == WHITE) {
                // Count white cells
                white_cells_count++;

                // Find the first white cell
                if (row == -1 && col == -1) {
                    row = i;
                    col = j;
                }
            }
        }
    }

    // Rule 3: When completed, all un-shaded (white) squares create a single continuous area
    return dfs_white_cells(block, visited, row, col) == white_cells_count;
}

bool check_hitori_conditions(BCB* block) {
    
    // Rule 1: No unshaded number appears in a row or column more than once
    // Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner

    int i, j, k;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {

            if (block->solution[i * board.cols_count + j] == UNKNOWN) return false;

            if (block->solution[i * board.cols_count + j] == WHITE) {
                for (k = 0; k < board.rows_count; k++) {
                    if (k != i && block->solution[k * board.cols_count + j] == WHITE && board.grid[i * board.cols_count + j] == board.grid[k * board.cols_count + j]) return false;
                }

                for (k = 0; k < board.cols_count; k++) {
                    if (k != j && block->solution[i * board.cols_count + k] == WHITE && board.grid[i * board.cols_count + j] == board.grid[i * board.cols_count + k]) return false;
                }
            }

            if (block->solution[i * board.cols_count + j] == BLACK) {
                if (i > 0 && block->solution[(i - 1) * board.cols_count + j] == BLACK) return false;
                if (i < board.rows_count - 1 && block->solution[(i + 1) * board.cols_count + j] == BLACK) return false;
                if (j > 0 && block->solution[i * board.cols_count + j - 1] == BLACK) return false;
                if (j < board.cols_count - 1 && block->solution[i * board.cols_count + j + 1] == BLACK) return false;
            }
        }
    }

    if (!all_white_cells_connected(block)) return false;

    return true;
}

/* ------------------ SOLUTION 5 BACKTRACKING ------------------ */

bool build_leaf(BCB* block, int uk_x, int uk_y) {

    /* if (!block->solution_space_unknowns[0] || !block->solution_space_unknowns[15] || !block->solution_space_unknowns[16]) {
        printf("[Build leaf] Error in solution space unknowns\n");
    } */
    
    int i, board_y_index;
    while (uk_x < board.rows_count && uk_y >= unknown_index_length[uk_x]) {
        uk_x++;
        uk_y = 0;
    }

    if (uk_x == board.rows_count) {
        /* if (shared_solution_space_solutions_to_skip > 0) {
            shared_solution_space_order -= 1;
            if (shared_solution_space_order == -1) 
                shared_solution_space_order = shared_solution_space_solutions_to_skip;
            else {
                printf("[Build leaf][%d] Skipping shared solution space %d %d\n", rank, shared_solution_space_order, shared_solution_space_solutions_to_skip);
                return false;
            }
            printf("[Build leaf][%d] Testing shared solution space %d %d\n", rank, shared_solution_space_order, shared_solution_space_solutions_to_skip);
        } */
        return true;
    }

    
    board_y_index = unknown_index[uk_x * board.cols_count + uk_y];

    CellState cell_state = block->solution[uk_x * board.cols_count + board_y_index];
    
    bool is_solution_space_unknown = block->solution_space_unknowns[uk_x * board.cols_count + uk_y];
    if (!is_solution_space_unknown) {
        if (cell_state == UNKNOWN)
            cell_state = WHITE;
    } else {
        printf("[Build leaf][%d] Reached solution space unknown\n", rank);
    }
    
    if (cell_state == UNKNOWN){
        printf("[Build leaf] Cell is unknown\n");
        exit(-1);
    }

    for (i = 0; i < 2; i++) {
        if (is_cell_state_valid(block, uk_x, board_y_index, cell_state)) {
            block->solution[uk_x * board.cols_count + board_y_index] = cell_state;
            if (build_leaf(block, uk_x, uk_y + 1))
                return true;
        }
        if (is_solution_space_unknown){
            printf("[Build leaf][%d] Skipping solution space unknown\n", rank);
            break;
        }
        cell_state = BLACK;
    }
    if (!is_solution_space_unknown)
        block->solution[uk_x * board.cols_count + board_y_index] = UNKNOWN;
    return false;
}

bool next_leaf(BCB *block) {
    int i, j, board_y_index;
    CellState cell_state;

    /* if (!block->solution_space_unknowns[0] || !block->solution_space_unknowns[15] || !block->solution_space_unknowns[16]) {
        printf("[Next leaf] Error in solution space unknowns\n");
    } */

    // find next white cell iterating unknowns from bottom
    for (i = board.rows_count - 1; i >= 0; i--) {
        for (j = unknown_index_length[i] - 1; j >= 0; j--) {
            board_y_index = unknown_index[i * board.cols_count + j];
            cell_state = block->solution[i * board.cols_count + board_y_index];

            if (block->solution_space_unknowns[i * board.cols_count + j]) {
                if (block->solution[i * board.cols_count + board_y_index] == UNKNOWN){
                    printf("[Next leaf] Solution space set unknown is unknown\n");
                    exit(-1);
                }
                printf("[Next leaf][%d] Reached end of solution space\n", rank);
                return false;
            }

            if (cell_state == UNKNOWN) {
                printf("\n\n\n\n\n\n\nCell is unknown\n");
                print_block("Block", block);
                printf("Unknown index: %d %d\n\n\n\n\n\n", i, board_y_index);
                // exit(-1);
                return false;
            }

            if (cell_state == WHITE) {
                if (is_cell_state_valid(block, i, board_y_index, BLACK)) {
                    block->solution[i * board.cols_count + board_y_index] = BLACK;
                    if(build_leaf(block, i, j + 1))
                        return true;
                }
            }

            block->solution[i * board.cols_count + board_y_index] = UNKNOWN;
        }
    }
    return false;
}

void init_solution_space(BCB* block, int solution_space_id) {
    
    block->solution = malloc(board.rows_count * board.cols_count * sizeof(CellState));
    block->solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool));

    memcpy(block->solution, board.solution, board.rows_count * board.cols_count * sizeof(CellState));
    memset(block->solution_space_unknowns, false, board.rows_count * board.cols_count * sizeof(bool));

    int i, j;
    int uk_idx, cell_choice, temp_solution_space_id = SOLUTION_SPACES - 1;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (unknown_index[i * board.rows_count + j] == -1)
                break;
            uk_idx = unknown_index[i * board.rows_count + j];
            cell_choice = solution_space_id % 2;

            // Validate if cell_choice (black or white) here is valid
            //      If not valid, use fixed choice and do not decrease solution_space_id
            //      If neither are valid, set to unknown (then the loop will change it)
            if (!is_cell_state_valid(block, i, uk_idx, cell_choice)) {
                cell_choice = abs(cell_choice - 1);
                if (!is_cell_state_valid(block, i, uk_idx, cell_choice)) {
                    cell_choice = UNKNOWN;
                    continue;
                }
            }

            block->solution[i * board.cols_count + uk_idx] = cell_choice;
            block->solution_space_unknowns[i * board.cols_count + j] = true;

            if (solution_space_id > 0)
                solution_space_id = solution_space_id / 2;
            
            if (temp_solution_space_id > 0)
                temp_solution_space_id = temp_solution_space_id / 2;
            
            if (temp_solution_space_id == 0)
                break;
        }

        if (temp_solution_space_id == 0)
            break;
    }
}

void block_to_buffer(BCB* block, int **buffer) {
    int i;
    printf("a\n");
    // *buffer = (int *) malloc((board.rows_count * board.cols_count * 2) * sizeof(int));
    memcpy(*buffer, block->solution, board.rows_count * board.cols_count * sizeof(CellState));
    printf("b\n");
    for (i = 0; i < board.rows_count * board.cols_count; i++)
        (*buffer)[board.rows_count * board.cols_count + i] = block->solution_space_unknowns[i] ? 1 : 0;
    printf("c\n");
}

void free_request(MPI_Request *request) {
    if (request != NULL) {
        // MPI_Request_free(request);
        request = NULL;
    }
}

bool buffer_to_block(int *buffer, BCB *block) {

    int i;
    block->solution = malloc(board.rows_count * board.cols_count * sizeof(CellState));
    block->solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool));
    
    // if (buffer[0] == UNKNOWN) {
    //     printf("[ERROR] Process %d received unknown buffer\n", rank);
    //     return false;
    // }
    
    memcpy(block->solution, buffer, board.rows_count * board.cols_count * sizeof(CellState));
    for (i = 0; i < board.rows_count * board.cols_count; i++) {
        block->solution_space_unknowns[i] = buffer[board.rows_count * board.cols_count + i] == 1;
    }

    return true;
}

void compute_unknowns(Board board, int **unknown_index, int **unknown_index_length) {
    int i, j, temp_index = 0, total = 0;
    *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));
    for (i = 0; i < board.rows_count; i++) {
        temp_index = 0;
        for (j = 0; j < board.cols_count; j++) {
            int cell_index = i * board.cols_count + j;
            if (board.solution[cell_index] == UNKNOWN){
                (*unknown_index)[i * board.cols_count + temp_index] = j;
                temp_index++;
            }
        }
        (*unknown_index_length)[i] = temp_index;
        total += temp_index;
        if (temp_index < board.cols_count)
            (*unknown_index)[i * board.cols_count + temp_index] = -1;
    }

    // print total unknowns
    if (rank == 0) printf("Total unknowns: %d\n", total);
}

/* ------------------ MESSAGE PASSING ------------------ */

void receive_message(Message *message, int source, MPI_Request *request, int tag) {
    if (source == rank) {
        printf("[ERROR] Process %d tried to receive a message from itself\n", rank);
        exit(-1);
    }
    int flag = 0;
    MPI_Status status;
    MPI_Test(request, &flag, &status);
    if (!flag)
        printf("[ERROR] Process %d tried to receive a message from process %d while the previous request is not completed\n", rank, source);
    else {
        // if(status.MPI_SOURCE == -2)
        //     printf("[ERROR] Process %d got -2 in status.MPI_SOURCE\n", rank);
        MPI_Irecv(message, 1, MPI_MESSAGE, source, tag, MPI_COMM_WORLD, request);
    }
}

void send_message(int destination, MPI_Request *request, MessageType type, int data1, int data2, bool invalid, int tag) {
    if (destination == rank) {
        printf("[ERROR] Process %d tried to send a message to itself\n", rank);
        exit(-1);
    }
    int flag = 0;
    MPI_Test(request, &flag, MPI_STATUS_IGNORE);
    if (!flag) {
        printf("[WARNING] Process %d tried to send a message to process %d while the previous request is not completed, waiting for it\n", rank, destination);
        MPI_Wait(request, MPI_STATUS_IGNORE);
        printf("[INFO] Process %d finished waiting for the previous request to complete\n", rank);
    }
    Message message = {type, data1, data2, invalid};
    MPI_Isend(&message, 1, MPI_MESSAGE, destination, tag, MPI_COMM_WORLD, request);
}

void init_requests_and_messages() {

    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_C_BOOL};
    int blocklengths[4] = {1, 1, 1, 1};

    Message dummy_message;
    MPI_Aint base_address, offsets[4];
    MPI_Get_address(&dummy_message, &base_address);
    MPI_Get_address(&dummy_message.type, &offsets[0]);
    MPI_Get_address(&dummy_message.data1, &offsets[1]);
    MPI_Get_address(&dummy_message.data2, &offsets[2]);
    MPI_Get_address(&dummy_message.invalid, &offsets[3]);

    offsets[0] -= base_address;
    offsets[1] -= base_address;
    offsets[2] -= base_address;
    offsets[3] -= base_address;

    MPI_Type_create_struct(4, blocklengths, offsets, types, &MPI_MESSAGE);
    MPI_Type_commit(&MPI_MESSAGE);

    if (rank == MANAGER_RANK) {
        rank_to_request_mapping = (int *) malloc(size * sizeof(int));
        worker_requests = (MPI_Request *) malloc((size - 1) * sizeof(MPI_Request));
        worker_messages = (Message *) malloc((size - 1) * sizeof(Message));
        worker_statuses = (WorkerStatus *) malloc((size) * sizeof(WorkerStatus));
        int i, count = 0;
        for (i = 0; i < size; i++) {
            worker_statuses[i].queue_size = 0;
            worker_statuses[i].processes_sharing_solution_space = 0;
            worker_statuses[i].master_process = -1;

            if (i == MANAGER_RANK) continue;
            worker_requests[count] = MPI_REQUEST_NULL;
            receive_message(&worker_messages[count], i, &worker_requests[count], W2M_MESSAGE);
            rank_to_request_mapping[i] = count;
            count++;
        }
    } else {
        manager_request = MPI_REQUEST_NULL;
        receive_work_request = MPI_REQUEST_NULL;
        refresh_solution_space_request = MPI_REQUEST_NULL;
        finished_solution_space_request = MPI_REQUEST_NULL;
        receive_work_buffer = (int *) malloc(board.cols_count * board.rows_count * 2 * sizeof(int));
        send_work_buffer = (int *) malloc(board.cols_count * board.rows_count * 2 * sizeof(int));
        processes_in_my_solution_space = (int *) malloc(size * sizeof(int));
        memset(processes_in_my_solution_space, -1, size * sizeof(int));
        receive_message(&manager_message, MANAGER_RANK, &manager_request, W2M_MESSAGE);
    }
}

void worker_receive_work(int source, Queue *queue) {
    int flag = 0;
    
    // --- receive initial message
    receive_message(&receive_work_message, source, &receive_work_request, W2W_MESSAGE);
    worker_wait_for_message(&receive_work_request, queue);
    if (terminated) return;

    if (receive_work_message.invalid) {
        printf("[ERROR] Process %d received an invalid message from process while receiving work %d\n", rank, source);
        // TODO: handle invalid message (maybe another ASK_FOR_WORK request??)
        return;
    }
    // TODO: handle solutions_to_skip and total_processes_in_solution_space
    solutions_to_skip = receive_work_message.data1;
    total_processes_in_solution_space = receive_work_message.data2;

    // --- receive buffer
    MPI_Status status;
    MPI_Test(&receive_work_request, &flag, &status);
    if (!flag || status.MPI_SOURCE != -2)
        printf("[ERROR] MPI_Test in worker RECEIVE_WORK failed with flag %d and status %d\n", flag, status.MPI_SOURCE);
    MPI_Irecv(receive_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, source, W2W_BUFFER, MPI_COMM_WORLD, &receive_work_request);
    worker_wait_for_message(&receive_work_request, queue);
    if (terminated) return;

    BCB block_to_receive;
    if (buffer_to_block(receive_work_buffer, &block_to_receive))
        enqueue(queue, &block_to_receive);

    // --- open refresh solution space message channel
    if (total_processes_in_solution_space > 1)
        receive_message(&refresh_solution_space_message, source, &refresh_solution_space_request, W2W_MESSAGE);
    else 
        is_my_solution_spaces_ended = true;
}

void worker_send_work(int destination, Queue *queue) {
    MPI_Request send_work_request;
    int queue_size = getQueueSize(queue);
    bool invalid_request = terminated || is_my_solution_spaces_ended || queue_size == 0;

    BCB block_to_send;
    int solutions_to_skip_to_send = 0;
    int total_processes_in_solution_space_to_send = 0;
    if (queue_size == 1) {
        block_to_send = peek(queue);
        block_to_buffer(&block_to_send, &send_work_buffer);
        solutions_to_skip_to_send = total_processes_in_solution_space;
        total_processes_in_solution_space_to_send = ++total_processes_in_solution_space;
        processes_in_my_solution_space[destination] = 1;
        solutions_to_skip = 0;

        // --- tell the workers in the same solution space to update the next solution space
        int i, count = 0;
        for (i = 0; i < size; i++) {
            if (rank == i || processes_in_my_solution_space[i] == -1 || i == destination) continue;
            MPI_Request request = MPI_REQUEST_NULL;
            send_message(i, &request, REFRESH_SOLUTION_SPACE, ++count, total_processes_in_solution_space, false, W2W_MESSAGE);
            
        }
        if (count != total_processes_in_solution_space_to_send - 2)
            printf("[ERROR] Process %d encountered wrong number of processes sharing solution space: %d %d\n", rank, count, total_processes_in_solution_space_to_send - 2);
        
        // --- open channel for finished solution space message
        Message finished_solution_space_message;
        receive_message(&finished_solution_space_message, MPI_ANY_SOURCE, &finished_solution_space_request, W2W_MESSAGE);
    }
    else if(queue_size > 1) {
        block_to_send = dequeue(queue);
        block_to_buffer(&block_to_send, &send_work_buffer);
        solutions_to_skip_to_send = 0;
        total_processes_in_solution_space_to_send = 0;
    }
    // --- send initial message
    send_message(destination, &send_work_request, WORKER_SEND_WORK, solutions_to_skip_to_send, total_processes_in_solution_space_to_send, invalid_request, W2W_MESSAGE);

    // --- send buffer
    MPI_Request send_work_buffer_request;
    MPI_Isend(send_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, destination, W2W_BUFFER, MPI_COMM_WORLD, &send_work_buffer_request);
}

void worker_check_messages(Queue *queue) {
    int flag = 1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        MPI_Test(&manager_request, &flag, &status);
        if (flag) {
            printf("[INFO] Process %d received a message from manager {%d}\n", rank, manager_message.type);
            if (manager_message.type == TERMINATE) {
                terminated = true;
            }
            else if (manager_message.type == SEND_WORK) {
                worker_send_work(manager_message.data1, queue);
            }
            else if (manager_message.type == RECEIVE_WORK) {
                worker_receive_work(manager_message.data1, queue);
            }
            else if(manager_message.type == FINISHED_SOLUTION_SPACE) {
                if (processes_in_my_solution_space[manager_message.data1] == -1)
                    printf("[ERROR] Process %d received an invalid FINISHED_SOLUTION_SPACE message from process %d (already -1)\n", rank, status.MPI_SOURCE);
                processes_in_my_solution_space[manager_message.data1] = -1;
            }
            else {
                printf("[ERROR] Process %d received an invalid message type %d from manager\n", rank, manager_message.type);
                // return &manager_message;
            }
            
            receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);
        }
    }

    if (is_my_solution_spaces_ended && total_processes_in_solution_space > 1) {
        flag = 0;
        MPI_Test(&refresh_solution_space_request, &flag, &status);
        if (flag) {
            if (refresh_solution_space_message.type == REFRESH_SOLUTION_SPACE) {
                int buffer_size = board.cols_count * board.rows_count * 2;
                int refresh_solution_space_buffer[buffer_size];
                if (status.MPI_SOURCE == -2)
                    printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for buffer solution refresh\n", rank);
                MPI_Irecv(refresh_solution_space_buffer, buffer_size, MPI_INT, status.MPI_SOURCE, W2W_BUFFER, MPI_COMM_WORLD, &refresh_solution_space_request);
                worker_wait_for_message(&refresh_solution_space_request, queue);

                receive_message(&refresh_solution_space_message, status.MPI_SOURCE, &refresh_solution_space_request, W2W_MESSAGE);
            }
        }
    }
}

void worker_wait_for_message(MPI_Request *request, Queue *queue) {
    int flag = 0;
    while(!flag && !terminated) {
        MPI_Test(request, &flag, MPI_STATUS_IGNORE);
        worker_check_messages(queue);
    }
}

void manager_consume_message(Message *message, int source) {
    printf("[INFO] Process %d (manager) received a message from process %d {%d}\n", rank, source, message->type);
    int i, sender_id;
    sender_id = rank_to_request_mapping[source];
    if (message->type == TERMINATE) {
        for (i = 0; i < size; i++) {
            if (i == MANAGER_RANK || i == message->data1) continue;
            send_message(i, &worker_requests[sender_id], TERMINATE, source, -1, false, M2W_MESSAGE);
        }
        terminated = true;
    }
    else if (message->type == STATUS_UPDATE) {
        worker_statuses[source].queue_size = message->data1;
        worker_statuses[source].processes_sharing_solution_space = message->data2;
    }
    else if (message->type == ASK_FOR_WORK) {
        worker_statuses[source].queue_size = 0;
        worker_statuses[source].processes_sharing_solution_space = 0;

        int min_queue_size = size + 1;
        int min_processes_sharing_solution_space = size + 1;
        int target_worker = -1;

        for (i = 0; i < size; i++) {
            if (i == source || worker_statuses[i].master_process == i) continue;
            if (worker_statuses[i].queue_size < min_queue_size && worker_statuses[i].queue_size > 0) {
                min_queue_size = worker_statuses[i].queue_size;
                min_processes_sharing_solution_space = worker_statuses[i].processes_sharing_solution_space;
                target_worker = i;
            } else if (worker_statuses[i].queue_size == min_queue_size && worker_statuses[i].processes_sharing_solution_space < min_processes_sharing_solution_space) {
                min_processes_sharing_solution_space = worker_statuses[i].processes_sharing_solution_space;
                target_worker = i;
            }
        }
        if (target_worker == -1) {
            printf("[INFO] Process %d (manager) could not find a target worker to assign work for process %d\n", rank, source);
            send_message(source, &worker_requests[sender_id], TERMINATE, rank, -1, false, M2W_MESSAGE);
        } else {
            send_message(target_worker, &worker_requests[rank_to_request_mapping[target_worker]], SEND_WORK, source, -1, false, M2W_MESSAGE);
            send_message(source, &worker_requests[sender_id], RECEIVE_WORK, target_worker, -1, false, M2W_MESSAGE);
            if (worker_statuses[source].master_process != -1 && worker_statuses[worker_statuses[source].master_process].master_process == -1) {
                MPI_Request finished_request = MPI_REQUEST_NULL;
                send_message(worker_statuses[source].master_process, &finished_request, FINISHED_SOLUTION_SPACE, source, -1, false, M2W_MESSAGE);
            }
            
            if (worker_statuses[target_worker].queue_size == 1) {
                worker_statuses[target_worker].queue_size = 1;
                worker_statuses[target_worker].processes_sharing_solution_space++;
                worker_statuses[source].queue_size = size + 1;
                worker_statuses[source].processes_sharing_solution_space = size + 1;
                worker_statuses[source].master_process = target_worker;
            } else if (worker_statuses[target_worker].queue_size > 1) {
                worker_statuses[target_worker].queue_size--;
                worker_statuses[source].queue_size = 1;
                worker_statuses[source].processes_sharing_solution_space = 1;
                if (worker_statuses[target_worker].master_process != -1)
                    printf("[ERROR] Process %d (manager) got invalid master process %d for target worker %d\n", rank, worker_statuses[target_worker].master_process, target_worker);
                worker_statuses[source].master_process = -1;
                if (worker_statuses[target_worker].processes_sharing_solution_space != 1)
                    printf("[ERROR] Process %d (manager) got invalid processes sharing solution space %d for target worker %d\n", rank, worker_statuses[target_worker].processes_sharing_solution_space, target_worker);
            } else
                printf("[ERROR] Process %d (manager) got invalid queue size %d for target worker %d\n", rank, worker_statuses[target_worker].queue_size, target_worker);
        }
    }
    else
        printf("[ERROR] Process %d (manager) received an invalid message type %d from process %d\n", rank, message->type, source);
}

void manager_check_messages() {
    int flag = 1, sender_id = -1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        MPI_Testany(size - 1, worker_requests, &sender_id, &flag, &status);

        if (flag) {

            // mapping between status.MPI_SOURCE and sender_id (corresponding to the index in the worker_requests array) [saved in rank_to_request_mapping]
            if (rank_to_request_mapping[status.MPI_SOURCE] != sender_id)
                printf("[ERROR] Process %d got error in mapping between status.MPI_SOURCE %d and sender_id %d, mapping: %d\n", rank, status.MPI_SOURCE, sender_id, rank_to_request_mapping[status.MPI_SOURCE]);

            manager_consume_message(&worker_messages[sender_id], status.MPI_SOURCE);
            receive_message(&worker_messages[sender_id], status.MPI_SOURCE, &worker_requests[sender_id], W2M_MESSAGE);
        }
    }
}

/* ------------------ MAIN ------------------ */

bool solution5() {

    int i, count = 0;
    // int solution_space_manager[SOLUTION_SPACES];
    int my_solution_spaces[SOLUTION_SPACES];
    
    // memset(solution_space_manager, -1, SOLUTION_SPACES * sizeof(int));
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));
    
    for (i = 0; i < SOLUTION_SPACES; i++) {
        // solution_space_manager[i] = i % size;
        if (i % size == rank) {
            my_solution_spaces[count++] = i;
        }
        if (rank == MANAGER_RANK) {
            worker_statuses[i % size].queue_size++;
            worker_statuses[i % size].processes_sharing_solution_space = 1;
        }
    }

    bool leaf_found = false;
    BCB blocks[SOLUTION_SPACES];

    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (my_solution_spaces[i] == -1) break;
        
        init_solution_space(&blocks[i], my_solution_spaces[i]);

        leaf_found = build_leaf(&blocks[i], 0, 0);

        if (check_hitori_conditions(&blocks[i])) {
            memcpy(board.solution, blocks[i].solution, board.rows_count * board.cols_count * sizeof(CellState));
            return true;
        }

        if (leaf_found) {
            enqueue(&solution_queue, &blocks[i]);
            count--;
        } else {
            printf("Processor %d failed to find a leaf\n", rank);
        }
    }
    // TODO: send status to manager if not all leafs found (CONVERT TO GATHER????)
    int queue_size = getQueueSize(&solution_queue);
    if (queue_size > 0 && count > 0) {
        MPI_Request status_update_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size, 1, false, W2M_MESSAGE);
    } else if (queue_size == 0) {
        is_my_solution_spaces_ended = true;
        MPI_Request ask_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
    }

    while(!terminated) {

        worker_check_messages(&solution_queue);
        if (rank == MANAGER_RANK) manager_check_messages();

        queue_size = getQueueSize(&solution_queue);
        if (queue_size > 0) {
            BCB current_solution = dequeue(&solution_queue);

            leaf_found = next_leaf(&current_solution);

            if (leaf_found) {
                if (check_hitori_conditions(&current_solution)) {
                    memcpy(board.solution, current_solution.solution, board.rows_count * board.cols_count * sizeof(CellState));
                    return true;
                } else {
                    enqueue(&solution_queue, &current_solution);
                }
            } else {
                // send update status to manager
                if (queue_size > 1) {
                    MPI_Request status_update_request = MPI_REQUEST_NULL;
                    send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size, 1, false, W2M_MESSAGE);
                } else if (queue_size == 1) {  // now 0
                    is_my_solution_spaces_ended = true;
                    printf("Processor %d is asking for work\n", rank);
                    MPI_Request ask_work_request = MPI_REQUEST_NULL;
                    send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
                }
            }
        }
    }

    return false;
}

int main(int argc, char** argv) {

    /*
        Initialize MPI environment
    */

    MPI_Init(&argc, &argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    initializeQueue(&solution_queue);

    /*
        Read the board from the input file
    */

    if (rank == 0) read_board(&board.grid, &board.rows_count, &board.cols_count, &board.solution);

    /*
        Commit the MPI_Datatype for the StatusMessage struct
    */
    
    /*
        Share the board with all the processes by packing the data into a single array

        ALTERNATIVE: use MPI_Datatype to create a custom datatype for the board (necessitate the struct to have non-dynamic arrays)
    */
    
    mpi_share_board();

    MPI_Barrier(MPI_COMM_WORLD);

    /*
        Apply the basic hitori pruning techniques to the board.
    */

    Board (*techniques[])(Board) = {
        mpi_uniqueness_rule,
        mpi_sandwich_rules,
        mpi_pair_isolation,
        mpi_flanked_isolation,
        mpi_corner_cases
    };

    int i;
    int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

    double pruning_start_time = MPI_Wtime();
    Board pruned_solution = techniques[0](board);
    for (i = 1; i < num_techniques; i++) {
        pruned_solution = combine_board_solutions(pruned_solution, techniques[i](pruned_solution), false);
        if (DEBUG && rank == 0) print_board("Partial", pruned_solution, SOLUTION);
    }

    /*
        Repeat the whiting and blacking pruning techniques until the solution doesn't change
    */

    bool changed = true;
    while (changed) {

        Board white_solution = mpi_set_white(pruned_solution);
        Board black_solution = mpi_set_black(pruned_solution);
        Board partial = combine_board_solutions(pruned_solution, white_solution, false);
        Board new_solution = combine_board_solutions(partial, black_solution, false);

        if (DEBUG && rank == 0) print_board("Partial", new_solution, SOLUTION);
        if (rank == 0) changed = !is_board_equal(pruned_solution, new_solution, SOLUTION);
        
        MPI_Bcast(&changed, 1, MPI_C_BOOL, 0, MPI_COMM_WORLD);

        if (changed) pruned_solution = new_solution;
    }
    double pruning_end_time = MPI_Wtime();

    memcpy(board.solution, pruned_solution.solution, board.rows_count * board.cols_count * sizeof(CellState));
    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    Board final_solution = deep_copy(pruned_solution);
    double recursive_start_time = MPI_Wtime();
    compute_unknowns(final_solution, &unknown_index, &unknown_index_length);

    bool solution_found = solution5();
    if (solution_found) {
        printf("[%d] Solution found\n", rank);
        print_board("Solution", board, SOLUTION);
    }
    printf("Processor %d is finished\n", rank);
    double recursive_end_time = MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD);

    /*
        Printing the pruned solution
    */

    if (rank == 0) print_board("Initial", board, BOARD);
    if (rank == 0) print_board("Pruned", pruned_solution, SOLUTION);
    
    /*
        Print all the times
    */
    
    if (rank == 0) printf("Time for pruning part: %f\n", pruning_end_time - pruning_start_time);
    MPI_Barrier(MPI_COMM_WORLD);
    printf("[%d] Time for recursive part: %f\n", rank, recursive_end_time - recursive_start_time);
    MPI_Barrier(MPI_COMM_WORLD);
    printf("[%d] Time for check hitori part: %f\n", rank, ctime);
    MPI_Barrier(MPI_COMM_WORLD);
    
    /*
        Write the final solution to the output file
    */

    if (solution_found) printf("Solution found by process %d\n", rank);
    if (solution_found) {
        write_solution(board);
        char formatted_string[MAX_BUFFER_SIZE];
        snprintf(formatted_string, MAX_BUFFER_SIZE, "\nSolution found by process %d", rank);
        print_board(formatted_string, board, SOLUTION);
    }

    /*
        Free the memory and finalize the MPI environment
    */

    // TODO: free MPI datatype
   
    free_memory((int *[]){board.grid, board.solution, pruned_solution.grid, pruned_solution.solution, final_solution.grid, final_solution.solution});
    MPI_Finalize();
    return 0;
}