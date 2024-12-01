#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>

#define MAX_BUFFER_SIZE 2048
#define DEBUG false
#define SOLUTION_SPACES 4

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

// typedef struct {
//     __int128_t items[SOLUTION_SPACES];
//     int front;
//     int rear;
// } Queue;

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

// TODO: comment these messages
typedef enum Message {
    TERMINATE = 1,
    SEND_JOB = 2,
    RECEIVE_JOB = 3,
    SEND_STATUS = 4,
    RECEIVE_STATUS = 5,
    FREE_STATUS = 6,
    SHARE_SOLUTION_SPACE = 7,
    PROCESS_FINISHED = 8
} Message;

typedef struct StatusMessage {
    int queue_size;
    int processes_per_solution_space;  // Number of processes working on the solution space (is set != 0 only when queue_size == 1)
} StatusMessage;

int rank, size, solver_process = -1;
double ctime = 0;
Board board;
Queue solution_queue;
int *unknown_index, *unknown_index_length;

MPI_Datatype MPI_StatusMessage;
MPI_Request send_status_request, receive_job_request, share_solution_request, stopping_request;
MPI_Request *send_job_requests, *free_status_requests;

int process_asking_status = 0; // counter for the process asking for the status
int count_processes_sharing_solution_space = 0; // Number of processes sharing own solution space (used by managers)
int *processes_sharing_solution_space; // List of processes sharing the solution space (used by managers)
int shared_solution_space_order = 0;  // Indicates the progressive number of solutions to skip when working on a shared solution space
int shared_solution_space_solutions_to_skip = 0;  // Indicates the number of solutions to skip when working on a shared solution space
bool is_my_solution_spaces_ended = false;  // Indicates if the process has finished working on its solution spaces and has asked for others
int* share_solution_buffer;
bool is_manager = false;
bool received_termination_signal = false;
bool temp_terminated = false;
int *processes_asking_status;
int *send_job_buffers;

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
        if (shared_solution_space_solutions_to_skip > 0) {
            shared_solution_space_order -= 1;
            if (shared_solution_space_order == -1) 
                shared_solution_space_order = shared_solution_space_solutions_to_skip;
            else {
                printf("[Build leaf][%d] Skipping shared solution space %d %d\n", rank, shared_solution_space_order, shared_solution_space_solutions_to_skip);
                return false;
            }
            printf("[Build leaf][%d] Testing shared solution space %d %d\n", rank, shared_solution_space_order, shared_solution_space_solutions_to_skip);
        }
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
    *buffer = (int *) malloc((board.rows_count * board.cols_count * 2 + 1) * sizeof(int));
    printf("a1\n");
    memcpy(*buffer, block->solution, board.rows_count * board.cols_count * sizeof(CellState));
    printf("b\n");
    if (*buffer[0] == UNKNOWN)
        printf("[ERROR] Process %d buffer 0 is already unknown\n", rank);
    if (is_my_solution_spaces_ended) {
        // TODO: write better?
        *buffer[0] = UNKNOWN;
        printf("[ERROR] Process %d buffer 0 is set to unknown\n", rank);
    }
    printf("c\n");
    for (i = 0; i < board.rows_count * board.cols_count; i++)
        (*buffer)[board.rows_count * board.cols_count + i] = block->solution_space_unknowns[i] ? 1 : 0;
    printf("d\n");
    (*buffer)[board.rows_count * board.cols_count * 2] = 0;
    printf("e\n");
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
    
    if (buffer[0] == UNKNOWN) {
        printf("[ERROR] Process %d received unknown buffer\n", rank);
        return false;
    }
    
    memcpy(block->solution, buffer, board.rows_count * board.cols_count * sizeof(CellState));
    for (i = 0; i < board.rows_count * board.cols_count; i++) {
        block->solution_space_unknowns[i] = buffer[board.rows_count * board.cols_count + i] == 1;
    }

    return true;
}

bool check_for_messages(Queue *solution_queue) {
    // returns TRUE if a termination message is received
    int i, message_received = 0;
    MPI_Status status;
    
    if (!received_termination_signal) {
        if(!temp_terminated) printf("[%d] MPI_Test 5\n", rank);
        MPI_Test(&stopping_request, &message_received, &status);
        if (message_received) {
            // TODO: send free status to all processes sharing solution space ?
            printf("Process %d received termination signal\n", rank);
            initializeQueue(solution_queue);
            // free_request(&stopping_request);
            received_termination_signal = true;
            return true;
        }
    }
    
    if (is_manager) {
        message_received = 0;
        if(!temp_terminated) printf("[%d] MPI_Test 6\n", rank);
        MPI_Test(&send_status_request, &message_received, &status);  // test if a send status request has been received
        // printf("Process %d received %d\n", rank, message_received);
        if (message_received) {
            
            if (status.MPI_SOURCE == -2)
                printf("[ERROR] Process %d received from source -2\n", rank);

            // send status message to the source process
            StatusMessage status_message;
            status_message.queue_size = getQueueSize(solution_queue);
            status_message.processes_per_solution_space = count_processes_sharing_solution_space;
            if (is_my_solution_spaces_ended)
                status_message.queue_size = size + 1;
            // If the source process is already sharing the solution space, set the maximum queue size
            for (i = 0; i < count_processes_sharing_solution_space; i++) {
                if (processes_sharing_solution_space[i] == status.MPI_SOURCE) {
                    status_message.queue_size = size + 1;
                    break;
                }
            }
            
            printf("Process %d received send status from process %d, process asking status %d, queue size %d, min processes %d\n", rank, status.MPI_SOURCE, process_asking_status, status_message.queue_size, status_message.processes_per_solution_space);

            MPI_Request receive_status_request = MPI_REQUEST_NULL;
            MPI_Isend(&status_message, 1, MPI_StatusMessage, status.MPI_SOURCE, RECEIVE_STATUS, MPI_COMM_WORLD, &receive_status_request);
            processes_asking_status[status.MPI_SOURCE] = 1;
            process_asking_status++;
            printf("Process %d waiting for free status or send job from %d\n", rank, status.MPI_SOURCE);
            // free_status_requests[status.MPI_SOURCE] = MPI_REQUEST_NULL;
            // send_job_requests[status.MPI_SOURCE] = MPI_REQUEST_NULL;
            MPI_Irecv(NULL, 0, MPI_INT, status.MPI_SOURCE, FREE_STATUS, MPI_COMM_WORLD, &free_status_requests[status.MPI_SOURCE]);
            MPI_Irecv(&send_job_buffers[status.MPI_SOURCE * 2], 2, MPI_INT, status.MPI_SOURCE, SEND_JOB, MPI_COMM_WORLD, &send_job_requests[status.MPI_SOURCE]);
            MPI_Irecv(NULL, 0, MPI_INT, MPI_ANY_SOURCE, SEND_STATUS, MPI_COMM_WORLD, &send_status_request);
        }
        
        if (process_asking_status){
            message_received = 0;
            if(!temp_terminated) printf("[%d] MPI_Test 7\n", rank);
            for (i = 0; i < size; i++) {
                if (processes_asking_status[i] != -1){
                    MPI_Test(&free_status_requests[i], &message_received, &status);  // test if a free status request has been received
                    if (message_received) {

                        printf("Process %d received free status from process %d\n", rank, status.MPI_SOURCE);

                        // if (process_asking_status != status.MPI_SOURCE) 
                        //     printf("[ERROR] Process %d received free status from process %d (should not happen as process_asking_status=%d)\n", rank, status.MPI_SOURCE, process_asking_status);
                        process_asking_status--;
                        processes_asking_status[i] = -1;
                        /* if (send_job_requests[status.MPI_SOURCE] != MPI_REQUEST_NULL) 
                            MPI_Cancel(&send_job_requests[status.MPI_SOURCE]);
                        else
                            printf("[ERROR] Process %d send job request is null\n", rank);
                        MPI_Wait(&send_job_requests[status.MPI_SOURCE], MPI_STATUS_IGNORE); */

                    }
                }
            }
            if (!message_received){
                if(!temp_terminated) printf("[%d] MPI_Test 8\n", rank);
                for (i = 0; i < size; i++) {
                    if (processes_asking_status[i] != -1){
                        MPI_Test(&send_job_requests[i], &message_received, &status);  // test if a send job request has been received

                        if (message_received) {

                            printf("Process %d received send job from process %d\n", rank, status.MPI_SOURCE);

                            // if (process_asking_status != status.MPI_SOURCE)
                            //     printf("[ERROR] Process %d received send job from process %d (should not happen as process_asking_status=%d)\n", rank, status.MPI_SOURCE, process_asking_status);
                            
                            // send block to the source process
                            BCB block_to_send;
                            int queue_size = getQueueSize(solution_queue);
                            int *buffer;

                            if (is_my_solution_spaces_ended)
                                printf("[ERROR] Process %d has ended its solution spaces, but was asked for job\n", rank);
                            bool invalid_queue_or_processes = send_job_buffers[status.MPI_SOURCE * 2] != queue_size || send_job_buffers[status.MPI_SOURCE * 2 + 1] != count_processes_sharing_solution_space;
                            if (invalid_queue_or_processes)
                                printf("[ERROR] Process %d has invalid send job buffer %d %d %d %d\n", rank, send_job_buffers[status.MPI_SOURCE * 2], getQueueSize(solution_queue), send_job_buffers[status.MPI_SOURCE * 2 + 1], count_processes_sharing_solution_space);
                            
                            if (queue_size == 1) {

                                block_to_send = peek(solution_queue);

                                if (!is_my_solution_spaces_ended && !invalid_queue_or_processes) {
                                    processes_sharing_solution_space[count_processes_sharing_solution_space++] = status.MPI_SOURCE;
                                    shared_solution_space_solutions_to_skip = count_processes_sharing_solution_space;
                                }

                                // send SHARE_SOLUTION_SPACE message to all other processes in processes_sharing_solution_space
                                
                                block_to_buffer(&block_to_send, &buffer);

                                MPI_Request share_solution_requests[count_processes_sharing_solution_space];
                                if (is_my_solution_spaces_ended || invalid_queue_or_processes) {
                                    buffer[0] = UNKNOWN;
                                    MPI_Isend(buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, status.MPI_SOURCE, SHARE_SOLUTION_SPACE, MPI_COMM_WORLD, &share_solution_requests[0]);
                                } else {
                                    int target_process, k;
                                    for (k = 0; k < count_processes_sharing_solution_space; k++) {
                                        target_process = processes_sharing_solution_space[k];
                                        if (target_process >= 0) {
                                            buffer[board.rows_count * board.cols_count * 2] = k + 1;
                                            printf("Process %d sending share solution to process %d\n", rank, target_process);
                                            MPI_Isend(buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, target_process, SHARE_SOLUTION_SPACE, MPI_COMM_WORLD, &share_solution_requests[k]);
                                            print_block("Received block", &block_to_send);
                                        } else {
                                            printf("[ERROR] Process %d has invalid target process %d\n", rank, target_process);
                                            break;
                                        }
                                    }
                                    shared_solution_space_order = 0;
                                }

                            } else if (queue_size > 1) {
                                block_to_send = dequeue(solution_queue);
                                block_to_buffer(&block_to_send, &buffer);
                                MPI_Request receive_job_request;
                                if(is_my_solution_spaces_ended) buffer[0] = UNKNOWN;
                                MPI_Isend(buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, status.MPI_SOURCE, RECEIVE_JOB, MPI_COMM_WORLD, &receive_job_request);
                            } else {
                                printf("[ERROR] Process %d has no solution spaces, queue_size %d, %d\n", rank, queue_size, isEmpty(solution_queue));
                                
                                block_to_send.solution = malloc(board.rows_count * board.cols_count * sizeof(CellState));
                                block_to_send.solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool));

                                memset(block_to_send.solution, UNKNOWN, board.rows_count * board.cols_count * sizeof(CellState));
                                memset(block_to_send.solution_space_unknowns, false, board.rows_count * board.cols_count * sizeof(bool));
                                
                                block_to_buffer(&block_to_send, &buffer);
                                buffer[0] = UNKNOWN;
                                MPI_Request receive_job_request;
                                printf("[ERROR] Process %d sending unknown buffer to %d\n", rank, status.MPI_SOURCE);
                                if(is_my_solution_spaces_ended) buffer[0] = UNKNOWN;
                                MPI_Isend(buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, status.MPI_SOURCE, RECEIVE_JOB, MPI_COMM_WORLD, &receive_job_request);
                                printf("[ERROR] Process %d sent unknown buffer\n", rank);
                            }

                            process_asking_status--;
                            processes_asking_status[i] = -1;
                            /* if (free_status_requests[processes_asking_status[i]] != MPI_REQUEST_NULL)
                                MPI_Cancel(&free_status_requests[processes_asking_status[i]]);
                            else
                                printf("[ERROR] Process %d free status request is null\n", rank);
                            MPI_Wait(&free_status_requests[processes_asking_status[i]], MPI_STATUS_IGNORE); */
                        }
                    }
                }
            }
        }
    }

    if (!process_asking_status && shared_solution_space_solutions_to_skip > 0 && is_my_solution_spaces_ended) {
        message_received = 0;
        if(!temp_terminated) printf("[%d] MPI_Test 9\n", rank);
        MPI_Test(&share_solution_request, &message_received, MPI_STATUS_IGNORE);  // test if a share solution request has been received
        if (message_received) {

            printf("Process %d received share solution from process %d\n", rank, status.MPI_SOURCE);

            // receive block from the source process
            BCB new_block;
            bool success = buffer_to_block(share_solution_buffer, &new_block);
            if (!success) {
                printf("[ERROR] %d Finishing without asking for other jobs\n", rank);
                return false;
            }

            shared_solution_space_order = share_solution_buffer[board.rows_count * board.cols_count * 2];
            printf("Process %d received shared solution space %d %d\n", rank, shared_solution_space_order, shared_solution_space_solutions_to_skip);
            print_block("Shared block", &new_block);
            shared_solution_space_solutions_to_skip++;
            initializeQueue(solution_queue);
            enqueue(solution_queue, &new_block);
        }
    }
    return false;
}

void ask_for_other_solution_space(Queue *solution_queue, int *solution_managers) {
    
    printf("[Ask for other solution space] Process %d\n", rank);

    is_my_solution_spaces_ended = true;
    shared_solution_space_solutions_to_skip = 0;
    shared_solution_space_order = 0;
    // count_processes_sharing_solution_space = 0;  // TODO: check?
    processes_sharing_solution_space = (int *) malloc(size * sizeof(int));
    // free_request(&send_status_request);

    int i, target_process = -1, received = 0;
    BCB new_block;
    
    new_block.solution = malloc(board.rows_count * board.cols_count * sizeof(CellState));
    new_block.solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool));
    
    // send SEND_STATUS message to all other processes
    printf("[Ask for other solution space] %d Sending status to all other processes\n", rank);
    MPI_Request send_status_requests[SOLUTION_SPACES];
    bool messages_sent_to_processes[size];
    for (i = 0; i < SOLUTION_SPACES; i++) {
        target_process = solution_managers[i];
        if (target_process != rank && !messages_sent_to_processes[target_process]) {
            MPI_Isend(NULL, 0, MPI_INT, target_process, SEND_STATUS, MPI_COMM_WORLD, &send_status_requests[i]);
            messages_sent_to_processes[target_process] = true;
        }
    }
    
    // receive status from other processes
    printf("[Ask for other solution space] %d Receiving status from other processes\n", rank);
    StatusMessage status_message[SOLUTION_SPACES];
    MPI_Request receive_status_requests[SOLUTION_SPACES];
    for (i = 0; i < SOLUTION_SPACES; i++) {
        receive_status_requests[i] = MPI_REQUEST_NULL;
        target_process = solution_managers[i];
        if (target_process != rank && messages_sent_to_processes[target_process]) {
            
            printf("[Ask for other solution space] %d Receiving status from process %d\n", rank, target_process);
            MPI_Irecv(&status_message[i], 1, MPI_StatusMessage, target_process, RECEIVE_STATUS, MPI_COMM_WORLD, &receive_status_requests[i]);
            if(!temp_terminated) printf("[%d] MPI_Test 12\n", rank);
            while (true) {
                MPI_Test(&receive_status_requests[i], &received, MPI_STATUS_IGNORE);
                if (received) break;
                // MPI_Test(&stopping_request, &stopping, MPI_STATUS_IGNORE);
                // if (stopping) {
                //     printf("[Ask for other solution space] %d Stopping request received in while 1\n", rank);
                //     return;
                // }
                if (check_for_messages(solution_queue)) {
                    printf("[Ask for other solution space] %d Received termination message while waiting for status\n", rank);
                    return;
                }
            }
        }
    }

    // choose the process with the lowest queue_size
    int min_queue_size = size + 1;
    int chosen_process = -1;
    for (i = 0; i < SOLUTION_SPACES; i++) {
        target_process = solution_managers[i];
        if (target_process == rank || !messages_sent_to_processes[target_process]) continue;
        if (status_message[target_process].queue_size < min_queue_size && status_message[target_process].queue_size > 0) {
            printf("[Ask for other solution space] %d Process %d selected queue size: %d %d %d\n", rank, target_process, status_message[target_process].queue_size, status_message[target_process].processes_per_solution_space, min_queue_size);
            min_queue_size = status_message[target_process].queue_size;
            chosen_process = target_process;
        }
        printf("[Ask for other solution space] %d Process %d queue size: %d %d\n", rank, target_process, status_message[target_process].queue_size, status_message[target_process].processes_per_solution_space);
    }
    
    if (chosen_process == -1 || min_queue_size == size + 1 || min_queue_size == 0) {
        printf("[Ask for other solution space] %d No process found with queue size > 0, sending free status to all\n", rank);
        MPI_Request send_free_status_requests[SOLUTION_SPACES];
        for (i = 0; i < SOLUTION_SPACES; i++) {
            target_process = solution_managers[i];
            send_free_status_requests[i] = MPI_REQUEST_NULL;
            if (target_process != rank && messages_sent_to_processes[target_process]) {
                printf("[Ask for other solution space] %d Sending free status to process %d\n", rank, target_process);
                MPI_Isend(NULL, 0, MPI_INT, target_process, FREE_STATUS, MPI_COMM_WORLD, &send_free_status_requests[i]);
            }
        }
        return;
    }

    int buffer[board.rows_count * board.cols_count * 2 + 1];
    int min_processes_per_solution_space = size + 1;
    if (min_queue_size == 1) {
        // choose the process with the lowest processes_per_solution_space
        for (i = 0; i < SOLUTION_SPACES; i++) {
            target_process = solution_managers[i];
            if (target_process == rank || !messages_sent_to_processes[target_process] || status_message[target_process].queue_size > 1 || status_message[target_process].queue_size == 0) continue;
            if (status_message[target_process].processes_per_solution_space < min_processes_per_solution_space) {
                min_processes_per_solution_space = status_message[target_process].processes_per_solution_space;
                chosen_process = target_process;
            }
        }
    }

    // Try to mix processes in the beginning when asking for other solution spaces
    int count_same_queue_processes = 0;
    int same_queue_processes_ids[SOLUTION_SPACES];
    for (i = 0; i < SOLUTION_SPACES; i++) {
        target_process = solution_managers[i];
        if (target_process == rank || !messages_sent_to_processes[target_process] || status_message[target_process].queue_size == 0) continue;
        if (status_message[target_process].queue_size == status_message[chosen_process].queue_size && status_message[target_process].processes_per_solution_space == status_message[chosen_process].processes_per_solution_space) {
            same_queue_processes_ids[count_same_queue_processes] = target_process;
            count_same_queue_processes++;
        }
    }
    // Select a random process from the same queue based on rank
    if (count_same_queue_processes > 0) {
        chosen_process = same_queue_processes_ids[rank % count_same_queue_processes];
        printf("[Ask for other solution space] %d Chosen process from same queue: %d %d %d %d\n", rank, chosen_process, count_same_queue_processes, min_queue_size, min_processes_per_solution_space);
    }

    printf("[Ask for other solution space] %d Chosen process: %d\n", rank, chosen_process);

    // send a message to the chosen process to get a solution space
    MPI_Request sendjob_request = MPI_REQUEST_NULL;
    int send_job_buffer[2] = {status_message[chosen_process].queue_size, status_message[chosen_process].processes_per_solution_space};
    MPI_Isend(send_job_buffer, 2, MPI_INT, chosen_process, SEND_JOB, MPI_COMM_WORLD, &sendjob_request);
    
    MPI_Request free_status_requests[SOLUTION_SPACES];
    for (i = 0; i < SOLUTION_SPACES; i++)
        free_status_requests[i] = MPI_REQUEST_NULL;
    for (i = 0; i < SOLUTION_SPACES; i++) {
        target_process = solution_managers[i];
        if (target_process != chosen_process && messages_sent_to_processes[target_process]) {
            printf("[Ask for other solution space] %d Sending free status to process %d\n", rank, target_process);
            MPI_Isend(NULL, 0, MPI_INT, target_process, FREE_STATUS, MPI_COMM_WORLD, &free_status_requests[i]);
        }
    }

    // receive block from chosen process
    printf("[Ask for other solution space] %d Receiving block from process %d\n", rank, chosen_process);
    if (min_queue_size == 1)
        MPI_Irecv(buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, chosen_process, SHARE_SOLUTION_SPACE, MPI_COMM_WORLD, &receive_job_request);
    else
        MPI_Irecv(buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, chosen_process, RECEIVE_JOB, MPI_COMM_WORLD, &receive_job_request);

    printf("[Ask for other solution space] %d Waiting for block from process %d\n", rank, chosen_process);

    MPI_Status status;
    if(!temp_terminated) printf("[%d] MPI_Test 34\n", rank);
    while (true) {
        MPI_Test(&receive_job_request, &received, &status);
        if (received) break;
        // MPI_Test(&stopping_request, &stopping, &status);
        // if (stopping) {
        //     printf("[Ask for other solution space] %d Stopping request received in while 2\n", rank);
        //     return;
        // }
        if (check_for_messages(solution_queue)) {
            printf("[Ask for other solution space] %d Received termination message while waiting for status\n", rank);
            return;
        }
    }

    printf("[Ask for other solution space] %d Received block from process %d\n", rank, chosen_process);

    if (!buffer_to_block(buffer, &new_block))
        return ask_for_other_solution_space(solution_queue, solution_managers);

    if (min_queue_size == 1) {
        shared_solution_space_order = buffer[board.rows_count * board.cols_count * 2];
        shared_solution_space_solutions_to_skip = min_processes_per_solution_space + 1;
        MPI_Irecv(share_solution_buffer, board.rows_count * board.cols_count * 2 + 1, MPI_INT, chosen_process, SHARE_SOLUTION_SPACE, MPI_COMM_WORLD, &share_solution_request);
        printf("[Ask for other solution space] %d Sharing solution space %d %d\n", rank, shared_solution_space_order, shared_solution_space_solutions_to_skip);
        print_block("Sending block", &new_block);
    }

    printf("[Ask for other solution space] %d Enqueueing block\n", rank);
    
    enqueue(solution_queue, &new_block);
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

/* ------------------ MAIN ------------------ */

bool solution5() {

    int i, count = 0;
    int solution_space_manager[SOLUTION_SPACES];
    int my_solution_spaces[SOLUTION_SPACES];
    
    memset(solution_space_manager, -1, SOLUTION_SPACES * sizeof(int));
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));
    
    for (i = 0; i < SOLUTION_SPACES; i++) {
        solution_space_manager[i] = i % size;
        if (i % size == rank) {
            my_solution_spaces[count++] = i;
            is_manager = true;
        }
    }

    /*
        For each process, initialize a background task that waits for a termination signal
    */
    
    MPI_Irecv(&solver_process, 1, MPI_INT, MPI_ANY_SOURCE, TERMINATE, MPI_COMM_WORLD, &stopping_request);
    
    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (solution_space_manager[i] == rank) {
            printf("Processor %d is manager for solution space %d\n", rank, i);
            MPI_Irecv(NULL, 0, MPI_INT, MPI_ANY_SOURCE, SEND_STATUS, MPI_COMM_WORLD, &send_status_request);
            break;
        }
    }

    // print solution space manager
    /* if (rank == 0) {
        printf("Processor %d:\n", rank);
        for (i = 0; i < SOLUTION_SPACES; i++) {
            printf("    %d: %d\n", i, solution_space_manager[i]);
        }
        printf("\n");
        
        printf("Processor %d:\n", rank);
        for (i = 0; i < SOLUTION_SPACES; i++) {
            printf("    %d: %d\n", i, my_solution_spaces[i]);
        }
        printf("\n");
    } */

    bool leaf_found = false;
    BCB blocks[SOLUTION_SPACES];

    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (my_solution_spaces[i] == -1) break;
        
        init_solution_space(&blocks[i], my_solution_spaces[i]);

        leaf_found = build_leaf(&blocks[i], 0, 0);

        // if (rank == 0) {
        //     printf("Testing solution\n");
        //     print_block("Block2", &blocks[i]);
        // }
        
        if (check_hitori_conditions(&blocks[i])) {
            memcpy(board.solution, blocks[i].solution, board.rows_count * board.cols_count * sizeof(CellState));
            return true;
        }

        if (leaf_found) {
            enqueue(&solution_queue, &blocks[i]);
        } else {
            printf("Processor %d failed to find a leaf\n", rank);
        }
    }

    // if (rank == 2) print_board("First", board, SOLUTION);

    if(isEmpty(&solution_queue)) {
        ask_for_other_solution_space(&solution_queue, solution_space_manager);
    }

    while(!isEmpty(&solution_queue)) {

        // if (rank == 2) printQueue(&solution_queue);

        BCB current_solution = dequeue(&solution_queue);  //TODO: check if it is possible that it calls dequeue on an empty queue

        // if (rank == 2) printQueue(&solution_queue);

        leaf_found = next_leaf(&current_solution);

        // if (rank == 2) printf("[%d] Leaf found: %d\n", rank, leaf_found);

        if (leaf_found) {
            // if (rank == 2) print_board("Testing solution", board, SOLUTION);
            if (check_hitori_conditions(&current_solution)) {
                memcpy(board.solution, current_solution.solution, board.rows_count * board.cols_count * sizeof(CellState));
                return true;
            } else {
                enqueue(&solution_queue, &current_solution);
            }
        }
        
        if (isEmpty(&solution_queue))
            ask_for_other_solution_space(&solution_queue, solution_space_manager);
        else if (check_for_messages(&solution_queue))
            break;
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
        Allocate the processes_sharing_solution_space array
    */
    
    processes_asking_status = (int *) malloc(size * sizeof(int));
    send_job_buffers = (int *) malloc(2 * size * sizeof(int));
    processes_sharing_solution_space = (int *) malloc(size * sizeof(int));
    share_solution_buffer = (int *) malloc((board.rows_count * board.cols_count * 2 + 1) * sizeof(int));
    memset(processes_sharing_solution_space, -1, size * sizeof(int));
    memset(share_solution_buffer, -1, (board.rows_count * board.cols_count * 2 + 1) * sizeof(int));
    memset(processes_asking_status, -1, size * sizeof(int));

    int i, count = 0;
    MPI_Request process_finished_requests[size - 1];
    send_job_requests = (MPI_Request *) malloc(size * sizeof(MPI_Request));
    free_status_requests = (MPI_Request *) malloc(size * sizeof(MPI_Request));
    for (i = 0; i < size; i++) {
        send_job_requests[i] = MPI_REQUEST_NULL;
        free_status_requests[i] = MPI_REQUEST_NULL;
        if (i != rank) {
            process_finished_requests[count] = MPI_REQUEST_NULL;
            MPI_Irecv(NULL, 0, MPI_INT, i, PROCESS_FINISHED, MPI_COMM_WORLD, &process_finished_requests[count++]);
        }
    }
    
    /*
        Commit the MPI_Datatype for the StatusMessage struct
    */

    MPI_Datatype types[2] = {MPI_INT, MPI_INT};
    int blocklengths[2] = {1, 1};

    StatusMessage dummy_message;
    MPI_Aint base_address, offsets[2];
    MPI_Get_address(&dummy_message, &base_address);
    MPI_Get_address(&dummy_message.queue_size, &offsets[0]);
    MPI_Get_address(&dummy_message.processes_per_solution_space, &offsets[1]);

    offsets[0] -= base_address;
    offsets[1] -= base_address;

    MPI_Type_create_struct(2, blocklengths, offsets, types, &MPI_StatusMessage);
    MPI_Type_commit(&MPI_StatusMessage);
    
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
    temp_terminated = true;
    MPI_Request termination_requests[size - 1];
    if (solution_found) {
        
        printf("[%d] Solution found\n", rank);
        print_board("Solution", board, SOLUTION);
        
        received_termination_signal = true;
        count = 0;
        for (i = 0; i < size; i++) {
            if (i != rank) {
                termination_requests[count] = MPI_REQUEST_NULL;
                printf("Sending termination signal from %d to %d\n", rank, i);
                MPI_Isend(&rank, 1, MPI_INT, i, TERMINATE, MPI_COMM_WORLD, &termination_requests[count++]);
            }
        }
    }
    
    printf("Processor %d is finished\n", rank);

    MPI_Request finished_requests[size - 1];
    count = 0;
    // send process finished message to all other processes
    for (i = 0; i < size; i++)
        if (i != rank) {
            printf("Sending process finished message from %d to %d\n", rank, i);
            finished_requests[count] = MPI_REQUEST_NULL;
            MPI_Isend(NULL, 0, MPI_INT, i, PROCESS_FINISHED, MPI_COMM_WORLD, &finished_requests[count++]);
        }

    /*
        All the processes that finish early, with the solution not been found, will remain idle.
        All the other, instead, will continue to search for the solution. The first process that
        finds the solution will notify all the other active processes to stop.

        Note: the barrier is necessary to avoid the master process to continue
    */  

    double recursive_end_time = MPI_Wtime();

    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    // wait all other processes finished messages and keep checking for messages
    MPI_Status statuses[size - 1];
    int process_finished_requests_completed = 0, err = 0;
    while(true) {
        // printf("[%d] MPI_Test 10\n", rank);
        err = MPI_Testall(size - 1, process_finished_requests, &process_finished_requests_completed, statuses);
        if (err != MPI_SUCCESS) {
            printf("error\n");
            for (i = 0; i < size - 1; i++) {
                if (statuses[i].MPI_ERROR != MPI_SUCCESS) {
                    
                    char err_str[MPI_MAX_ERROR_STRING];
                    int err_len;
                    MPI_Error_string(statuses[i].MPI_ERROR, err_str, &err_len);
                    printf("MPI_Test error: %s\n", err_str);
                    printf("[ERROR] Process %d received error from process %d - %d\n", rank, statuses[i].MPI_SOURCE, statuses[i].MPI_ERROR);
                }
            }
        }
        if (process_finished_requests_completed) {
            printf("[%d] exiting\n", rank);
            break;
        }
        check_for_messages(&solution_queue);
    }

    sleep(1);

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

    free_memory((int *[]){board.grid, board.solution, pruned_solution.grid, pruned_solution.solution, final_solution.grid, final_solution.solution});

    MPI_Finalize();

    return 0;
}