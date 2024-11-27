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
    int *solution;
} Board;

typedef struct {
    __int128_t items[SOLUTION_SPACES];
    int front;
    int rear;
} Queue;

int rank, size, solver_process = -1;
double ctime = 0;
Board board;
Queue solution_queue;
MPI_Request stopping_request;

/* ------------------ GENERAL HELPERS ------------------ */

void read_board(int **board, int *rows_count, int *cols_count, int **solution) {

    /*
        Helper function to read the board from the input file.
    */
    
    FILE *fp = fopen("../test-cases/inputs/input-6x6.txt", "r");
    
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

    if (rows != cols) {
        printf("The board must be a square.\n");
        exit(1);
    }

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
    
    printf("# --- %s --- #\n", title);

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
    printf("\n");
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

void pack_board(Board board, int **buffer, int *buffer_size) {

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

void unpack_board(int *buffer, int buffer_size, Board *board) {
    
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

Board transpose(Board board) {

    /*
        Helper function to transpose a matrix.
    */

    Board Tboard = board;
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

    int i, j;
    for (i = 0; i < first_board.rows_count; i++) {
        for (j = 0; j < first_board.cols_count; j++) {
            if (type == BOARD && first_board.grid[i * first_board.cols_count + j] != second_board.grid[i * second_board.cols_count + j]) return false;
            if (type == SOLUTION  && first_board.solution[i * first_board.cols_count + j] != second_board.solution[i * second_board.cols_count + j]) return false;
        }
    }

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
void initializeQueue(Queue* q)
{
    q->front = -1;
    q->rear = -1;
}

int isFull(Queue* q)
{
    // If the next position is the front, the queue is full
    return (q->rear + 1) % SOLUTION_SPACES == q->front;
}

// Function to check if the queue is empty
bool isEmpty(Queue* q) {
    return q->front == -1;
}

void enqueue(Queue *q, __int128_t value)
{
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
    q->items[q->rear] = value;
    //printf("Element %lld inserted\n", value);
}

__int128_t dequeue(Queue* q)
{
    // If the queue is empty, print an error message and
    // return -1
    if (isEmpty(q)) {
        printf("Queue underflow\n");
        return -1;
    }
    // Get the data from the front of the queue
    __int128_t data = q->items[q->front];
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

void printQueue(Queue* q)
{
    // If the queue is empty, print a message and return
    if (isEmpty(q)) {
        printf("Queue is empty\n");
        return;
    }
    // Print the elements in the queue
    printf("Queue elements: ");
    int i = q->front;
    while (i != q->rear) {
        printf("%lld ", q->items[i]);
        i = (i + 1) % SOLUTION_SPACES;
    }
    // Print the last element
    printf("%lld\n", q->items[q->rear]);
}
/* ------------------ MPI UTILS ------------------ */

void mpi_share_board(Board board, Board *local_board) {

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

/* ------------------ HITORI BACKTRACKING ------------------ */

bool single_is_cell_state_valid(Board board, int x, int y, CellState cell_state) {
    if (cell_state == BLACK) {
        if (x > 0 && board.solution[(x - 1) * board.cols_count + y] == BLACK) return false;
        if (x < board.rows_count - 1 && board.solution[(x + 1) * board.cols_count + y] == BLACK) return false;
        if (y > 0 && board.solution[x * board.cols_count + y - 1] == BLACK) return false;
        if (y < board.cols_count - 1 && board.solution[x * board.cols_count + y + 1] == BLACK) return false;
    } else if (cell_state == WHITE) {
        int i, j, cell_value = board.grid[x * board.cols_count + y];
        // TODO: optimize this (if rows=columns) or use a sum table
        for (i = 0; i < board.rows_count; i++)
            if (i != x && board.grid[i * board.cols_count + y] == cell_value && board.solution[i * board.cols_count + y] == WHITE)
                return false;
        for (j = 0; j < board.cols_count; j++)
            if (j != y && board.grid[x * board.cols_count + j] == cell_value && board.solution[x * board.cols_count + j] == WHITE)
                return false;
    }
    return true;
}   

Board single_set_white_and_black_cells(Board board, int x, int y, CellState cell_state, int **edited_cells, int *edited_count) {
    
    if (edited_cells != NULL) {
        *edited_cells = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
        *edited_count = 0;
    }
    
    int i;
    if (cell_state == BLACK) {
        int points[4];
        int count = 0;

        if (x > 0) points[count++] = (x - 1) * board.cols_count + y;
        if (x < board.rows_count - 1) points[count++] = (x + 1) * board.cols_count + y;
        if (y > 0) points[count++] = x * board.cols_count + y - 1;
        if (y < board.cols_count - 1) points[count++] = x * board.cols_count + y + 1;

        for (i = 0; i < count; i++) {
            if (board.solution[points[i]] == UNKNOWN) {
                board.solution[points[i]] = WHITE;
                if (edited_cells != NULL) {
                    (*edited_cells)[*edited_count] = points[i];
                    (*edited_count)++;
                }
            }
        }
    } else if (cell_state == WHITE) {
        int i, j, cell_value = board.grid[x * board.cols_count + y];
        for (i = 0; i < board.rows_count; i++)
            if (i != x && board.grid[i * board.cols_count + y] == cell_value && board.solution[i * board.cols_count + y] == UNKNOWN) {
                board.solution[i * board.cols_count + y] = BLACK;
                if (edited_cells != NULL) {
                    (*edited_cells)[*edited_count] = i * board.cols_count + y;
                    (*edited_count)++;
                }
            }
        for (j = 0; j < board.cols_count; j++)
            if (j != y && board.grid[x * board.cols_count + j] == cell_value && board.solution[x * board.cols_count + j] == UNKNOWN) {
                board.solution[x * board.cols_count + j] = BLACK;
                if (edited_cells != NULL) {
                    (*edited_cells)[*edited_count] = x * board.cols_count + j;
                    (*edited_count)++;
                }
            }
    }
    return board;
}

Board single_restore_white_and_black_cells(Board board, int *edited_cells, int edited_count) {
    int i;
    for (i = 0; i < edited_count; i++)
        board.solution[edited_cells[i]] = UNKNOWN;
    return board;
}

int dfs_white_cells(Board board, bool * visited, int row, int col) {
    if (row < 0 || row >= board.rows_count || col < 0 || col >= board.cols_count) return 0;
    if (visited[row * board.cols_count + col]) return 0;
    if (board.solution[row * board.cols_count + col] == BLACK) return 0;

    visited[row * board.cols_count + col] = true;

    int count = 1;
    count += dfs_white_cells(board, visited, row - 1, col);
    count += dfs_white_cells(board, visited, row + 1, col);
    count += dfs_white_cells(board, visited, row, col - 1);
    count += dfs_white_cells(board, visited, row, col + 1);
    return count;
}

bool all_white_cells_connected(Board board) {

    bool *visited = malloc((board.rows_count * board.cols_count) * sizeof(bool));
    int i, j;

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            visited[i * board.cols_count + j] = false;
        }
    }

    // Find the first white cell
    int row = -1, col = -1;

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == WHITE) {
                row = i;
                col = j;
                break;
            }
        }
        if (row != -1) break;
    }

    int white_cells_count = 0;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == WHITE) white_cells_count++;
        }
    }

    // Rule 3: When completed, all un-shaded (white) squares create a single continuous area
    return dfs_white_cells(board, visited, row, col) == white_cells_count;
}

bool check_hitori_conditions(Board board) {
    
    // Rule 1: No unshaded number appears in a row or column more than once
    // Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner

    int i, j, k;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {

            if (board.solution[i * board.cols_count + j] == UNKNOWN) return false;

            if (board.solution[i * board.cols_count + j] == WHITE) {
                for (k = 0; k < board.rows_count; k++) {
                    if (k != i && board.solution[k * board.cols_count + j] == WHITE && board.grid[i * board.cols_count + j] == board.grid[k * board.cols_count + j]) return false;
                }

                for (k = 0; k < board.cols_count; k++) {
                    if (k != j && board.solution[i * board.cols_count + k] == WHITE && board.grid[i * board.cols_count + j] == board.grid[i * board.cols_count + k]) return false;
                }
            }

            if (board.solution[i * board.cols_count + j] == BLACK) {
                if (i > 0 && board.solution[(i - 1) * board.cols_count + j] == BLACK) return false;
                if (i < board.rows_count - 1 && board.solution[(i + 1) * board.cols_count + j] == BLACK) return false;
                if (j > 0 && board.solution[i * board.cols_count + j - 1] == BLACK) return false;
                if (j < board.cols_count - 1 && board.solution[i * board.cols_count + j + 1] == BLACK) return false;
            }
        }
    }

    if (!all_white_cells_connected(board)) return false;

    return true;
}

bool single_recursive_set_cell(Board board, int* unknown_index, int* unknown_index_length, int uk_x, int uk_y) {
    
    int i, board_y_index;
    //printf("trying %d %d\n", uk_x, uk_y);
    //print_vector(unknown_index, board.rows_count * board.cols_count);
    //print_board("Recursive", board, SOLUTION);
    //printf("Il valore della cella è %d\n", board.solution[uk_x * board.cols_count + unknown_index[uk_x * board.cols_count + uk_y]]);
    // Get next unknown starting from previous unknown in uk_x, uk_y

    int stopping_flag = 0;

    MPI_Test(&stopping_request, &stopping_flag, MPI_STATUS_IGNORE);
    
    if (stopping_flag) {
        if (DEBUG) printf("Process %d received termination signal\n", rank);
        return true;
    }
    
    while (uk_x < board.rows_count && uk_y >= unknown_index_length[uk_x]) {
        uk_x++;
        uk_y = 0;
    }


    if (uk_x == board.rows_count) {
        // TODO: validate current solution and return bool
        //printf("Solution found\n");

        // if (DEBUG) print_board("Solution", board, SOLUTION);

        bool is_hitori_valid = check_hitori_conditions(board);

        if (is_hitori_valid) {
            // if (DEBUG) printf("Solved by process %d\n", rank);
                
            for (i = 0; i < size; i++) {
                if (i != rank) {
                    MPI_Send(&rank, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                    if (DEBUG) printf("Sending termination signal from %d to %d\n", rank, i);
                }
            }
            solver_process = rank;
        }

        return is_hitori_valid;
    }

    board_y_index = unknown_index[uk_x * board.cols_count + uk_y];
    //printf("UK trovato: Provo la cella %d %d\n", uk_x, uk_y);

    // First check if setting this cell to white works, then try black
    int cell_value = board.solution[uk_x * board.cols_count + board_y_index];
    if (cell_value == UNKNOWN) {
        //printf("Provo il bianco %d %d\n", uk_x, uk_y);
        int *edited_cells, edited_count;
        if (single_is_cell_state_valid(board, uk_x, board_y_index, WHITE)) {
            board.solution[uk_x * board.cols_count + board_y_index] = WHITE;
            single_set_white_and_black_cells(board, uk_x, board_y_index, WHITE, &edited_cells, &edited_count);
            
            // TODO: check if board is passed by reference and the recursive function sees the updated solution
            if (single_recursive_set_cell(board, unknown_index, unknown_index_length, uk_x, uk_y + 1))
                return true;
            
            board = single_restore_white_and_black_cells(board, edited_cells, edited_count);
        } //else printf("Non posso mettere il bianco %d %d\n", uk_x, uk_y);
        
        //printf("Provo il nero %d %d\n", uk_x, uk_y);
        if (single_is_cell_state_valid(board, uk_x, board_y_index, BLACK)) {
            board.solution[uk_x * board.cols_count + board_y_index] = BLACK;
            single_set_white_and_black_cells(board, uk_x, board_y_index, BLACK, &edited_cells, &edited_count);
            // TODO: check if board is passed by reference and the recursive function sees the updated solution
            if (single_recursive_set_cell(board, unknown_index, unknown_index_length, uk_x, uk_y + 1))
                return true;

            board = single_restore_white_and_black_cells(board, edited_cells, edited_count);
        } //else printf("Non posso mettere il nero %d %d\n", uk_x, uk_y);
    
        board.solution[uk_x * board.cols_count + board_y_index] = UNKNOWN;

    } else if (single_is_cell_state_valid(board, uk_x, board_y_index, cell_value)) {
        //printf("Not unknown %d %d\n", uk_x, uk_y);
        bool solution_found = single_recursive_set_cell(board, unknown_index, unknown_index_length, uk_x, uk_y + 1);

        //printf("Forced solution %d %d %d %d\n", uk_x, uk_y, cell_value, solution_found);

        if (solution_found) return true;

        cell_value = abs(cell_value - 1);
        if (single_is_cell_state_valid(board, uk_x, board_y_index, cell_value)) {
            board.solution[uk_x * board.cols_count + board_y_index] = cell_value;
            solution_found = single_recursive_set_cell(board, unknown_index, unknown_index_length, uk_x, uk_y + 1);

            //printf("Forced solution [2] %d %d %d %d\n", uk_x, uk_y, cell_value, solution_found);
        }
    }

    return false;
}

void restore_unknowns(Board *ref_board, int *unknown_index, int *unknown_index_length) {
    Board board = *ref_board;
    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < unknown_index_length[i]; j++) {
            board.solution[i * board.cols_count + unknown_index[i * board.cols_count + j]] = UNKNOWN;
        }
    }
}

bool solution2_update_combinations(Board *ref_board, int *combinations_per_number, int *n_combinations_per_number, int *unknown_index, int *unknown_index_length, int combination_index, int combination_increase) {

    Board board = *ref_board;

    if (combination_index == 0) {
        printf("Invalid combination index\n");
        return false;
    }
    
    bool valid = true;
    int final_combination = combination_index + combination_increase;
    int i, j, unknown_index_y, cell_value, cell_combinations_number, temp_combination_number = 1;

    restore_unknowns(&board, unknown_index, unknown_index_length);
    
    int *combination_modified = malloc(board.cols_count * sizeof(int));
    for (i = board.rows_count - 1; i >= 0; i--) {
        memset(combination_modified, 0, board.cols_count * sizeof(int));
        for (j = unknown_index_length[i] - 1; j >= 0; j--) {
            unknown_index_y = unknown_index[i * board.cols_count + j];
            cell_value = board.grid[i * board.cols_count + unknown_index_y] - 1;
            if (combination_modified[cell_value] == 0) {
                cell_combinations_number = n_combinations_per_number[i * board.cols_count + cell_value] + 1;
                combinations_per_number[i * board.cols_count + cell_value] = (((final_combination - 1) / temp_combination_number) % cell_combinations_number) + 1;
                temp_combination_number *= cell_combinations_number;
            }
            combination_modified[cell_value]++;
        
            if (combinations_per_number[i * board.cols_count + cell_value] == combination_modified[cell_value]) 
                board.solution[i * board.cols_count + unknown_index_y] = WHITE;
            else
                board.solution[i * board.cols_count + unknown_index_y] = BLACK;

            valid = single_is_cell_state_valid(board, i, unknown_index_y, board.solution[i * board.cols_count + unknown_index_y]);

            if (!valid) break;
        }
    }

    free(combination_modified);

    return valid;
}

bool solution3_recursive_set_cell(Board *ref_board, int* unknown_index, int* unknown_index_length, int uk_x, int uk_y, int* solutions_to_skip) {

    Board board = *ref_board;

    //printf("trying %d %d\n", uk_x, uk_y);
    //print_vector(unknown_index, board.rows_count * board.cols_count);
    //print_board("Recursive", board, SOLUTION);
    //printf("Il valore della cella è %d\n", board.solution[uk_x * board.cols_count + unknown_index[uk_x * board.cols_count + uk_y]]);

    // Get next unknown starting from previous unknown in uk_x, uk_y

    int i, board_y_index, stopping_flag = 0;

    MPI_Test(&stopping_request, &stopping_flag, MPI_STATUS_IGNORE);
    
    if (stopping_flag) {
        if (DEBUG) printf("Process %d received termination signal\n", rank);
        return true;
    }
    
    while (uk_x < board.rows_count && uk_y >= unknown_index_length[uk_x]) {
        uk_x++;
        uk_y = 0;
    }


    if (uk_x == board.rows_count) {
        if (*solutions_to_skip > 0) {
            // printf("[%d] Skipping solution %d\n", rank, *solutions_to_skip);
            (*solutions_to_skip)--;
            return false;
        } else {
            *solutions_to_skip = size - 1;
            // printf("[%d] Testing solution %d\n", rank, *solutions_to_skip);
        }

        double ttime = MPI_Wtime();
        bool is_hitori_valid = check_hitori_conditions(board);
        ctime += MPI_Wtime() - ttime; 

        if (is_hitori_valid) {
            if (DEBUG) printf("Solved by process %d\n", rank);
            for (i = 0; i < size; i++) {
                if (i != rank) {
                    MPI_Send(&rank, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                    if (DEBUG) printf("Sending termination signal from %d to %d\n", rank, i);
                }
            }
            solver_process = rank;
        }

        return is_hitori_valid;
    }

    board_y_index = unknown_index[uk_x * board.cols_count + uk_y];
    //printf("UK trovato: Provo la cella %d %d\n", uk_x, uk_y);

    // First check if setting this cell to white works, then try black
    int cell_value = board.solution[uk_x * board.cols_count + board_y_index];
    if (cell_value == UNKNOWN) {
        //printf("Provo il bianco %d %d\n", uk_x, uk_y);
        int *edited_cells, edited_count;
        if (single_is_cell_state_valid(board, uk_x, board_y_index, WHITE)) {
            board.solution[uk_x * board.cols_count + board_y_index] = WHITE;
            single_set_white_and_black_cells(board, uk_x, board_y_index, WHITE, &edited_cells, &edited_count);
            
            // TODO: check if board is passed by reference and the recursive function sees the updated solution
            if (solution3_recursive_set_cell(&board, unknown_index, unknown_index_length, uk_x, uk_y + 1, solutions_to_skip))
                return true;
            
            board = single_restore_white_and_black_cells(board, edited_cells, edited_count);
        } //else printf("Non posso mettere il bianco %d %d\n", uk_x, uk_y);
        
        //printf("Provo il nero %d %d\n", uk_x, uk_y);
        if (single_is_cell_state_valid(board, uk_x, board_y_index, BLACK)) {
            board.solution[uk_x * board.cols_count + board_y_index] = BLACK;
            single_set_white_and_black_cells(board, uk_x, board_y_index, BLACK, &edited_cells, &edited_count);
            // TODO: check if board is passed by reference and the recursive function sees the updated solution
            if (solution3_recursive_set_cell(&board, unknown_index, unknown_index_length, uk_x, uk_y + 1, solutions_to_skip))
                return true;

            board = single_restore_white_and_black_cells(board, edited_cells, edited_count);
        } //else printf("Non posso mettere il nero %d %d\n", uk_x, uk_y);
    
        board.solution[uk_x * board.cols_count + board_y_index] = UNKNOWN;

    } else if (single_is_cell_state_valid(board, uk_x, board_y_index, cell_value)) {
        //printf("Not unknown %d %d\n", uk_x, uk_y);
        bool solution_found = solution3_recursive_set_cell(&board, unknown_index, unknown_index_length, uk_x, uk_y + 1, solutions_to_skip);

        //printf("Forced solution %d %d %d %d\n", uk_x, uk_y, cell_value, solution_found);

        if (solution_found) return true;

        // cell_value = abs(cell_value - 1);
        // if (single_is_cell_state_valid(board, uk_x, board_y_index, cell_value)) {
        //     board.solution[uk_x * board.cols_count + board_y_index] = cell_value;
        //     solution_found = solution3_recursive_set_cell(&board, unknown_index, unknown_index_length, uk_x, uk_y + 1, solutions_to_skip);

        //     //printf("Forced solution [2] %d %d %d %d\n", uk_x, uk_y, cell_value, solution_found);
        // }
    }

    return false;
}

bool solution4_build_leaf(Board *ref_board, int* unknown_index, int* unknown_index_length, int uk_x, int uk_y, int uk_idx, __int128_t *solution_id, __int128_t *solution_rest) {
    Board board = *ref_board;
    
    int i, board_y_index;
    while (uk_x < board.rows_count && uk_y >= unknown_index_length[uk_x]) {
        uk_x++;
        uk_y = 0;
    }

    if (uk_x == board.rows_count) {
        // TODO: validate solution
        // if (rank == 1) print_board("Testing solution", board, SOLUTION);
        // if (check_hitori_conditions(board)) {
        //     printf("Solution found\n");
        //     return true;
        // }
        // return false;
        return true;
    }

    board_y_index = unknown_index[uk_x * board.cols_count + uk_y];

    // if (rank == 1) printf("Trying %d %d %d\n", uk_x, uk_y, uk_idx);

    __int128_t uk_idx_pow = pow(2, uk_idx);
    __int128_t local_rest = 0;
    CellState cell_state = ((*solution_id) / uk_idx_pow) % 2;
    
    // if (rank == 1) printf("Cell state: %d, pow: %lld\n", cell_state, uk_idx_pow);
    
    for (i = 0; i < 2; i++) {
        // if (rank == 1) printf("Trying cell %d: %lld", cell_state, *solution_id);
        if (single_is_cell_state_valid(board, uk_x, board_y_index, cell_state)) {
            board.solution[uk_x * board.cols_count + board_y_index] = cell_state;
            local_rest = cell_state * uk_idx_pow;
            *solution_rest += local_rest;
            // if (rank == 1) printf(" - Valid, rest: %lld\n", *solution_rest);
            if (solution4_build_leaf(ref_board, unknown_index, unknown_index_length, uk_x, uk_y + 1, uk_idx + 1, solution_id, solution_rest))
                return true;
            *solution_rest -= local_rest;
            // if (rank == 1) printf(" - Local rest %lld -- ", local_rest);
            // if (rank == 1) printf("Restoring %d %d %d: %lld\n", uk_x, uk_y, uk_idx, *solution_rest); 
        } else {
            // if (rank == 1) printf(" - Invalid\n");
        }
        
        // Change cell state
        if (cell_state == WHITE) {
            cell_state = BLACK;
            *solution_id = *solution_id - (*solution_id - *solution_rest) + uk_idx_pow;
            // if (rank == 1) printf("Changing to black: %lld\n", *solution_id);
        }
        else
            break;
    }
    board.solution[uk_x * board.cols_count + board_y_index] = UNKNOWN;
    return false;
}

bool solution4_next_leaf(Board *ref_board, int* unknown_index, int* unknown_index_length, __int128_t *solution_id) {

    // compute total unknowns
    int total_unknowns = 0, i, j;
    for (i = 0; i < board.rows_count; i++)
        for (j = 0; j < board.cols_count; j++)
            if (board.solution[i * board.cols_count + j] == UNKNOWN)
                total_unknowns++;
    
    int temp_unknowns = total_unknowns;
    int uk_x, uk_y, uk_idx_pow;
    // find next white cell iterating unknowns from bottom
    for (i = board.rows_count - 1; i >= 0; i--) {
        for (j = unknown_index_length[i] - 1; j >= 0; j--) {
            uk_x = i;
            uk_y = unknown_index[i * board.cols_count + j];
            uk_idx_pow = pow(2, temp_unknowns);
            if (board.solution[uk_x * board.cols_count + uk_y] == WHITE) {
                if (single_is_cell_state_valid(board, uk_x, uk_y, BLACK)) {
                    board.solution[uk_x * board.cols_count + uk_y] = BLACK;
                    *solution_id += uk_idx_pow;
                    __int128_t *temp_rest = malloc(sizeof(__int128_t));
                    *temp_rest = *solution_id;
                    if(solution4_build_leaf(ref_board, unknown_index, unknown_index_length, uk_x, uk_y + 1, temp_unknowns, solution_id, temp_rest))
                        return true;
                    *solution_id -= uk_idx_pow;
                }
            } else {
                *solution_id -= uk_idx_pow;
            }
            board.solution[uk_x * board.cols_count + uk_y] = UNKNOWN;
            temp_unknowns--;
        }
    }
    return false;
}

// 1010  ->  101-  ->  10--  ->  0110 -> 6
// 1010(10) - 1000(= 10 - 2) = 0010 + 100(pow(2, 2)) = 0110
// 10101X111  -> 000001111 (101010111 + 1000) - 101010000
// 101010111 - 101010000(= 101010111 - 111) = 000000111 + 1000 = 000001111

// 100101 (10X101)
// 100101(69) - 100000(= 100101 - 101) = 000101 + 1000(pow(2, 3)) = 001101
// 100000 -> 69 - 5
/* ------------------ MAIN ------------------ */

void solution1(Board final_solution) {
    int i, j, temp_index = 0;
    int *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    int *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));
    if (rank == 0) {
        // Initialize the unknown index matrix with the indexes of the unknown cells to better scan them
        for (i = 0; i < board.rows_count; i++) {
            temp_index = 0;
            for (j = 0; j < board.cols_count; j++) {
                int cell_index = i * board.cols_count + j;
                if (final_solution.solution[cell_index] == UNKNOWN){
                    unknown_index[i * board.cols_count + temp_index] = j;
                    temp_index++;
                }
            }
            unknown_index_length[i] = temp_index;
            if (temp_index < board.cols_count)
                unknown_index[i * board.cols_count + temp_index] = -1;
        }

        //print_vector(unknown_index, board.rows_count * board.cols_count);
        //print_vector(unknown_index_length, board.rows_count);
    }

    MPI_Bcast(unknown_index, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(unknown_index_length, board.rows_count, MPI_INT, 0, MPI_COMM_WORLD);

    // Create initial starting solution depending on the rank of the process
    // In this way, we define a sort of solution space for each process to analyse
    int uk_idx, cell_choice, temp_rank = rank;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (unknown_index[i * board.rows_count + j] == -1)
                break;
            uk_idx = unknown_index[i * board.rows_count + j];
            cell_choice = temp_rank % 2;

            //if (rank == 0) printf("[Cell choice] Rank %d: i=%d, j=%d, uk_idx=%d, cell_choice=%d\n", rank, i, j, uk_idx, cell_choice);

            // Validate if cell_choice (black or white) here is valid
            //      If not valid, use fixed choice and do not decrease temp_rank
            //      If neither are valid, set to white (then the loop will change it)
            if (!single_is_cell_state_valid(final_solution, i, uk_idx, cell_choice)) {
                cell_choice = abs(cell_choice - 1);
                if (!single_is_cell_state_valid(final_solution, i, uk_idx, cell_choice)) {
                    cell_choice = UNKNOWN;
                    continue;
                }
            }

            //if (rank == 1) printf("[Updated] Rank %d: i=%d, j=%d, uk_idx=%d, cell_choice=%d\n", rank, i, j, uk_idx, cell_choice);

            final_solution.solution[i * board.cols_count + uk_idx] = cell_choice;
            final_solution = single_set_white_and_black_cells(final_solution, i, uk_idx, cell_choice, NULL, NULL);
            //unknown_index[i * board.cols_count + j] = -2;  // ??
            // set white and black cells (localised single process, no need to check all the matrix)
            //      remember to update unknown_index (maybe add -2 as a value to ignore)

            //if (rank == 1) print_board("[After coloring]", final_solution, SOLUTION);

            if (temp_rank > 0)
                temp_rank = temp_rank / 2;
            
            if (temp_rank == 0)
                break;
        }

        if (temp_rank == 0)
            break;
    }

    //print_board("Pruned sad", final_solution, SOLUTION);
    // Loop
    // recursive function iterating unknown_index
    single_recursive_set_cell(final_solution, unknown_index, unknown_index_length, 0, 0);
}

void solution2(Board board) {
    int i, j, temp_index = 0;
    int *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    int *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));
    if (rank == 0) {
        // Initialize the unknown index matrix with the indexes of the unknown cells to better scan them
        for (i = 0; i < board.rows_count; i++) {
            temp_index = 0;
            for (j = 0; j < board.cols_count; j++) {
                int cell_index = i * board.cols_count + j;
                if (board.solution[cell_index] == UNKNOWN){
                    unknown_index[i * board.cols_count + temp_index] = j;
                    temp_index++;
                }
            }
            unknown_index_length[i] = temp_index;
            if (temp_index < board.cols_count)
                unknown_index[i * board.cols_count + temp_index] = -1;
        }

        //print_vector(unknown_index, board.rows_count * board.cols_count);
        //print_vector(unknown_index_length, board.rows_count);
    }

    MPI_Bcast(unknown_index, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(unknown_index_length, board.rows_count, MPI_INT, 0, MPI_COMM_WORLD);

    // first row of matrix has 2 cells with value=1
    // if cpn of row 0 of value 2 is 1, then the first cell with value=1 from the left is white
    // if cpn of row 0 of value 2 is 2, then the second cell with value=1 from the left is white
    // if cpn of row 0 of value 2 is 3 (ncpn+1), then all cells with value=1 are black
    int *combinations_per_number = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    int *n_combinations_per_number = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));

    memset(combinations_per_number, 0, board.rows_count * board.cols_count * sizeof(int));
    memset(n_combinations_per_number, 0, board.rows_count * board.cols_count * sizeof(int));

    int value, total_combinations = 1;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < unknown_index_length[i]; j++) {
            value = board.grid[i * board.cols_count + unknown_index[i * board.cols_count + j]];
            combinations_per_number[i * board.cols_count + value - 1] = 1;
            n_combinations_per_number[i * board.cols_count + value - 1]++;
        }
    }
    // print numbers of combinations per number
    if(rank == 0) {
        for (i = 0; i < board.rows_count; i++) {
            for (j = 0; j < board.rows_count; j++) {
                printf("%d ", n_combinations_per_number[i * board.cols_count + j]);
            }
            printf("\n");
        }
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    sleep(1);
    
    for (i = 0; i < board.rows_count * board.cols_count; i++) {
        if (n_combinations_per_number[i] == 0) continue;
        total_combinations *= n_combinations_per_number[i] + 1;
    }

    MPI_Bcast(combinations_per_number, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(n_combinations_per_number, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) printf("Total combinations: %d\n", total_combinations);

    int stopping_flag = 0;
    int cycles_count = 0;
    
    for (i = 1; i <= total_combinations; i += size) {

        if (cycles_count % 50 == 0) {
            MPI_Test(&stopping_request, &stopping_flag, MPI_STATUS_IGNORE);

            if (stopping_flag) break;
        }

        bool is_valid = solution2_update_combinations(&board, combinations_per_number, n_combinations_per_number, unknown_index, unknown_index_length, i, rank);
        
        if (!is_valid) continue;

        /*if (rank == 0 && DEBUG) {
            char formatted_string[MAX_BUFFER_SIZE];
            snprintf(formatted_string, MAX_BUFFER_SIZE, "\n[%d] Solution %d", rank, i);
            print_board(formatted_string, board, SOLUTION);
        }*/

        if (check_hitori_conditions(board)) {
            char formatted_string[MAX_BUFFER_SIZE];
            snprintf(formatted_string, MAX_BUFFER_SIZE, "\n[%d] Solution found .)", rank);
            print_board(formatted_string, board, SOLUTION);

            for (i = 0; i < size; i++) {
                if (i != rank) {
                    MPI_Send(&rank, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                    if (DEBUG) printf("Sending termination signal from %d to %d\n", rank, i);
                }
            }
            solver_process = rank;

            break;
        }

        cycles_count++;
    }
    
    //MPI_Barrier(MPI_COMM_WORLD);

    /* MPI_Finalize();

    exit(0);*/
}

void solution3(Board final_solution) {
    
    int i, j, temp_index = 0;
    int *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    int *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));
    if (rank == 0) {
        // Initialize the unknown index matrix with the indexes of the unknown cells to better scan them
        for (i = 0; i < board.rows_count; i++) {
            temp_index = 0;
            for (j = 0; j < board.cols_count; j++) {
                int cell_index = i * board.cols_count + j;
                if (final_solution.solution[cell_index] == UNKNOWN){
                    unknown_index[i * board.cols_count + temp_index] = j;
                    temp_index++;
                }
            }
            unknown_index_length[i] = temp_index;
            if (temp_index < board.cols_count)
                unknown_index[i * board.cols_count + temp_index] = -1;
        }

        //print_vector(unknown_index, board.rows_count * board.cols_count);
        //print_vector(unknown_index_length, board.rows_count);
    }

    MPI_Bcast(unknown_index, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(unknown_index_length, board.rows_count, MPI_INT, 0, MPI_COMM_WORLD);

    //print_board("Pruned sad", final_solution, SOLUTION);
    // Loop
    // recursive function iterating unknown_index
    int solutions_to_skip = rank;
    solution3_recursive_set_cell(&final_solution, unknown_index, unknown_index_length, 0, 0, &solutions_to_skip);
}

void solution4(Board board) {
    

    // ======================================= UNKNOWN INDEX ======================================= //

    int i, j, temp_index = 0;
    int *unknown_index = (int *) malloc(board.rows_count * board.cols_count * sizeof(int));
    int *unknown_index_length = (int *) malloc(board.rows_count * sizeof(int));
    if (rank == 0) {
        // Initialize the unknown index matrix with the indexes of the unknown cells to better scan them
        for (i = 0; i < board.rows_count; i++) {
            temp_index = 0;
            for (j = 0; j < board.cols_count; j++) {
                int cell_index = i * board.cols_count + j;
                if (board.solution[cell_index] == UNKNOWN){
                    unknown_index[i * board.cols_count + temp_index] = j;
                    temp_index++;
                }
            }
            unknown_index_length[i] = temp_index;
            if (temp_index < board.cols_count)
                unknown_index[i * board.cols_count + temp_index] = -1;
        }
    }

    MPI_Bcast(unknown_index, board.rows_count * board.cols_count, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(unknown_index_length, board.rows_count, MPI_INT, 0, MPI_COMM_WORLD);


    // ======================================= COMBINATIONS COUNT ======================================= //
    

    __int128_t total_combinations;

    int unknown_count = 0;
    for (i = 0; i < board.rows_count; i++)
        unknown_count += unknown_index_length[i];
     
    if (unknown_count > 127) {
        printf("Too many unknown cells\n");
        return;
    }
    total_combinations = pow(2, unknown_count);

    // print __int128_t
    
    printf("%lld\n", total_combinations);



    // ======================================= SOLUTION 4 ======================================= //


    __int128_t solution_space_size = total_combinations / SOLUTION_SPACES;
    __int128_t start_solution = rank;
    __int128_t solution_rest;
    //__int128_t end_solution = (rank + 1) * solution_space_size;

    // TODO: manage when processors different then SOLUTION SPACES

    int count = 0;
    int temp_space_manager[SOLUTION_SPACES];
    memset(temp_space_manager, -1, SOLUTION_SPACES * sizeof(int));
    for (j = rank; j < SOLUTION_SPACES; j += size)
        temp_space_manager[count++] = j;

    int solution_space_manager[size][SOLUTION_SPACES];
    MPI_Allgather(temp_space_manager, SOLUTION_SPACES, MPI_INT, solution_space_manager, SOLUTION_SPACES, MPI_INT, MPI_COMM_WORLD);

    // for (i = 0; i < SOLUTION_SPACES; i++) {
    //     printf("Processor %d, %d: %d\n", rank, i, solution_space_manager[rank][i]);
    // }
    bool leaf_found = false;

    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (solution_space_manager[rank][i] == -1) break;
        if (solution_space_manager[rank][i] == rank) {
            leaf_found = solution4_build_leaf(&board, unknown_index, unknown_index_length, 0, 0, 0, &start_solution, &solution_rest);
            
            if (check_hitori_conditions(board)) {
                printf("[%d] Solution found\n", rank);
                return;
            }

            enqueue(&solution_queue, start_solution);
            break;
        }
    }

    if (rank == 1) {

        while(!isEmpty(&solution_queue)) {

            //if (rank == 0) printQueue(&solution_queue);

            __int128_t current_solution = dequeue(&solution_queue);

            //if (rank == 0) printQueue(&solution_queue);

            leaf_found = solution4_next_leaf(&board, unknown_index, unknown_index_length, &current_solution);

            // TODO: add check for reaching solution space end
            if (leaf_found) {
                // if (rank == 1) print_board("Testing solution", board, SOLUTION);
                if (check_hitori_conditions(board)) {
                    printf("[%d] Solution found\n", rank);
                    print_board("Solution", board, SOLUTION);
                    return;
                } else {
                    enqueue(&solution_queue, current_solution);
                }

            } else {
                // TODO: if no leaf found, ask other processes for their solution spaces
            }
        }
    }
    
    printf("Processor %d is finished\n", rank);
    
    // TODO: take work from other processes
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
    
    // TODO: check that board is NxN with max number equal to N
    
    /*
        Share the board with all the processes by packing the data into a single array

        ALTERNATIVE: use MPI_Datatype to create a custom datatype for the board (necessitate the struct to have non-dynamic arrays)
    */
    
    mpi_share_board(board, &board);

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

    int i;
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

    /*
        For each process, initialize a background task that waits for a termination signal
    */
    
    MPI_Irecv(&solver_process, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &stopping_request);

    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    Board final_solution = deep_copy(pruned_solution);
    double recursive_start_time = MPI_Wtime();
    
    // solution1(final_solution);

    // solution2(final_solution);

    // solution3(final_solution);

    solution4(final_solution);


    /*
        All the processes that finish early, with the solution not been found, will remain idle.
        All the other, instead, will continue to search for the solution. The first process that
        finds the solution will notify all the other active processes to stop.

        Note: the barrier is necessary to avoid the master process to continue
    */  

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

    printf("[%d] Solution found by process %d\n", rank, solver_process);

    if (rank == solver_process) {
        write_solution(final_solution);
        char formatted_string[MAX_BUFFER_SIZE];
        snprintf(formatted_string, MAX_BUFFER_SIZE, "\nSolution found by process %d", rank);
        print_board(formatted_string, final_solution, SOLUTION);
    }

    /*
        Free the memory and finalize the MPI environment
    */

    free_memory((int *[]){board.grid, board.solution, pruned_solution.grid, pruned_solution.solution, final_solution.grid, final_solution.solution});

    MPI_Finalize();

    return 0;
}