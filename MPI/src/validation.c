#include <stdlib.h> 
#include <string.h>
#include <stdio.h>

#include "../include/validation.h"

typedef struct {
    int x;
    int y;
} Cell;

typedef struct {
    Cell *data;
    int front;
    int rear;
    int capacity;
    int size;
} ValidationQueue;

ValidationQueue* createQueue(int capacity) {
    ValidationQueue *queue = (ValidationQueue *)malloc(sizeof(ValidationQueue));
    queue->data = (Cell *)malloc(capacity * sizeof(Cell));
    queue->front = 0;
    queue->rear = -1;
    queue->capacity = capacity;
    queue->size = 0;
    return queue;
}

bool isValidationQueueEmpty(ValidationQueue *queue) {
    return queue->size == 0;
}

void val_enqueue(ValidationQueue *queue, int x, int y) {
    if (queue->size == queue->capacity) {
        queue->capacity *= 2;
        queue->data = (Cell *)realloc(queue->data, queue->capacity * sizeof(Cell));
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->data[queue->rear].x = x;
    queue->data[queue->rear].y = y;
    queue->size++;
}

Cell val_dequeue(ValidationQueue *queue) {
    if (isValidationQueueEmpty(queue)) {
        printf("[ERROR] Queue underflow: Attempting to dequeue from an empty queue.\n");
        exit(-1);
    }
    Cell cell = queue->data[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    return cell;
}

void freeQueue(ValidationQueue *queue) {
    free(queue->data);
    free(queue);
}

bool is_cell_state_valid(Board board, BCB* block, int x, int y, CellState cell_state) {
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

int bfs_white_cells(Board board, BCB *block, bool *visited, int row, int col) {
    int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int count = 0, i;

    ValidationQueue *queue = createQueue(board.rows_count * board.cols_count);
    val_enqueue(queue, row, col); // assuming the cell is white
    visited[row * board.cols_count + col] = true;

    while(!isValidationQueueEmpty(queue)) {
        Cell cell = val_dequeue(queue);
        count++;

        for (i = 0; i < 4; i++) {
            int new_row = cell.x + directions[i][0];
            int new_col = cell.y + directions[i][1];

            if (new_row >= 0 && new_row < board.rows_count && new_col >= 0 && new_col < board.cols_count) {
                if (!visited[new_row * board.cols_count + new_col] && block->solution[new_row * board.cols_count + new_col] == WHITE) {
                    val_enqueue(queue, new_row, new_col);
                    visited[new_row * board.cols_count + new_col] = true;
                }
            }
        }
    }

    freeQueue(queue);
    return count;
}

int dfs_white_cells(Board board, BCB *block, bool* visited, int row, int col) {
    if (row < 0 || row >= board.rows_count || col < 0 || col >= board.cols_count) return 0;
    if (visited[row * board.cols_count + col]) return 0;
    if (block->solution[row * board.cols_count + col] == BLACK) return 0;

    visited[row * board.cols_count + col] = true;

    int count = 1;
    count += dfs_white_cells(board, block, visited, row - 1, col);
    count += dfs_white_cells(board, block, visited, row + 1, col);
    count += dfs_white_cells(board, block, visited, row, col - 1);
    count += dfs_white_cells(board, block, visited, row, col + 1);
    return count;
}

bool all_white_cells_connected(Board board, BCB* block) {

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
    return dfs_white_cells(board, block, visited, row, col) == white_cells_count;

    // return bfs_white_cells(board, block, visited, row, col) == white_cells_count;
}

bool check_hitori_conditions(Board board, BCB* block) {
    
    // Rule 1: No unshaded number appears in a row or column more than once
    // Rule 2: Shaded cells cannot be adjacent, although they can touch at a corner

    // Rule 3: When completed, all un-shaded (white) squares create a single continuous area

    if (!all_white_cells_connected(board, block)) return false;

    return true;
}
