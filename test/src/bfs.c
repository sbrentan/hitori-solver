#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h>

// Define the Cell and Queue structs (same as before)
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
} Queue;

Queue* createQueue(int capacity) {
    Queue *queue = (Queue *)malloc(sizeof(Queue));
    queue->data = (Cell *)malloc(capacity * sizeof(Cell));
    queue->front = 0;
    queue->rear = -1;
    queue->capacity = capacity;
    queue->size = 0;
    return queue;
}

bool isQueueEmpty(Queue *queue) {
    return queue->size == 0;
}

void enqueue(Queue *queue, int x, int y) {
    if (queue->size == queue->capacity) {
        queue->capacity *= 2;
        queue->data = (Cell *)realloc(queue->data, queue->capacity * sizeof(Cell));
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->data[queue->rear].x = x;
    queue->data[queue->rear].y = y;
    queue->size++;
}

Cell dequeue(Queue *queue) {
    if (isQueueEmpty(queue)) {
        fprintf(stderr, "Queue underflow: Attempting to dequeue from an empty queue.\n");
        exit(EXIT_FAILURE);
    }
    Cell cell = queue->data[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    return cell;
}

void freeQueue(Queue *queue) {
    free(queue->data);
    free(queue);
}

// BFS Function
int bfs_white_cells_parallel(int rows, int cols, int *solution, bool *visited, int start_row, int start_col) {
    int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int count = 0, i, d;

    // Create the queue and enqueue the starting cell
    Queue *queue = createQueue(rows * cols);
    enqueue(queue, start_row, start_col);
    visited[start_row * cols + start_col] = true;

    // printf("Enqueued cell (%d, %d)\n", start_row, start_col);

    // Parallel region
    #pragma omp parallel private(i, d)
    {
        while (!isQueueEmpty(queue)) {
            int queue_size = queue->size;

            // printf("Thread %d: Batch size: %d\n", omp_get_thread_num(), queue_size);

            #pragma omp for reduction(+:count)
            for (i = 0; i < queue_size; i++) {
                Cell current;

                // Dequeue a cell (critical section to avoid race conditions)
                #pragma omp critical
                {
                    if (!isQueueEmpty(queue))
                        current = dequeue(queue);
                }

                count++;

                // Explore neighbors
                for (d = 0; d < 4; d++) {
                    int new_row = current.x + directions[d][0];
                    int new_col = current.y + directions[d][1];

                    // Check bounds and if the cell is unvisited and WHITE
                    if (new_row >= 0 && new_row < rows &&
                        new_col >= 0 && new_col < cols &&
                        !visited[new_row * cols + new_col] &&
                        solution[new_row * cols + new_col] == 0) {
                        
                        #pragma omp critical
                        {
                            if (!visited[new_row * cols + new_col]) {
                                visited[new_row * cols + new_col] = true;
                                enqueue(queue, new_row, new_col);
                                // printf("Thread %d: Enqueued cell (%d, %d)\n", omp_get_thread_num(), new_row, new_col);
                            }
                        }
                    }
                }
            }
        }
    }

    freeQueue(queue);
    return count;
}

// BFS Function
int test1(int rows, int cols, int *solution, bool *visited, int start_row, int start_col) {
    int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int count = 0, visited_count = 0, d;

    int num_threads = omp_get_max_threads();

    // Create the local queues
    Queue *local_queues[num_threads];

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        local_queues[tid] = createQueue(rows * cols / num_threads);
    }

    enqueue(local_queues[0], start_row, start_col);
    visited[start_row * cols + start_col] = true;
    visited_count++;

    // printf("Enqueued cell (%d, %d)\n", start_row, start_col);

    // Parallel region
    #pragma omp parallel shared(visited_count) private(d) reduction(+:count)
    {
        int tid = omp_get_thread_num();
        Queue *queue = local_queues[tid];

        while (visited_count < rows * cols || !isQueueEmpty(queue)) {
            int queue_size = queue->size;

            // printf("Thread %d: Batch size: %d\n", omp_get_thread_num(), queue_size);

            if (queue_size == 0) continue; 

            Cell current = dequeue(queue);
            count++;

            // Explore neighbors
            for (d = 0; d < 4; d++) {
                int new_row = current.x + directions[d][0];
                int new_col = current.y + directions[d][1];

                // Check bounds and if the cell is unvisited and WHITE
                if (new_row >= 0 && new_row < rows &&
                    new_col >= 0 && new_col < cols &&
                    !visited[new_row * cols + new_col]) {

                    #pragma omp critical
                    {
                        visited[new_row * cols + new_col] = true;
                        visited_count++;

                        if (solution[new_row * cols + new_col] == 0) {
                            // select random thread 
                            enqueue(local_queues[tid], new_row, new_col);
                            // printf("Thread %d: Enqueued cell (%d, %d)\n", omp_get_thread_num(), new_row, new_col);
                        }
                    }
                }
            }
        }

        freeQueue(queue);
    }

    return count;
}

void print_solution(int *solution, int rows, int cols) {
    int i, j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            printf("%d ", solution[i * cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}

void print_visited(bool *visited, int rows, int cols) {
    int i, j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            printf("%d ", visited[i * cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}

int main() {

    int rows = 20;
    int cols = 20;

    int *solution = (int[400]){
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
        0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    // print_solution(solution, rows, cols);

    bool *visited = (bool[400]){false};

    // print_visited(visited, rows, cols);

    // find the indexes of the first white cell
    int start_row = -1, start_col = -1;
    int i, j, white_count = 0;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            if (solution[i * cols + j] == 0) {
                white_count++;

                if (start_row == -1 && start_col == -1){
                    start_row = i;
                    start_col = j;
                }
            }
        }
    }

    printf("Starting cell: (%d, %d)\n", start_row, start_col);

    double start_time = omp_get_wtime();
    // int connected_white_count = bfs_white_cells_parallel(rows, cols, solution, visited, start_row, start_col);
    int connected_white_count = bfs_white_cells_parallel_allin(rows, cols, solution, visited, start_row, start_col);
    // int connected_white_count = test1(rows, cols, solution, visited, start_row, start_col);
    double end_time = omp_get_wtime();

    printf("Execution time: %f seconds\n", end_time - start_time);

    printf("Total white cells: %d, Total connected white cells: %d\n", white_count, connected_white_count);

    return 0;
}
