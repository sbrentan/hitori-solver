#include <stdio.h>
#include <stdlib.h>

#include "../include/queue.h"

// Function to initialize the queue
void initializeQueue(Queue* q, int size) {
    q->items = (BCB*) malloc(size * sizeof(BCB));
    q->front = -1;
    q->rear = -1;
    q->size = size;
}

void initializeQueueArray(Queue **leaf_queues, int array_size, int queue_size) {
    *leaf_queues = malloc(array_size * sizeof(Queue));
    if (*leaf_queues == NULL) {
        fprintf(stderr, "Memory allocation failed for leaf queues.\n");
        exit(-1);
    }
    int i;
    for (i = 0; i < array_size; i++) {
        initializeQueue(&(*leaf_queues)[i], queue_size);
    }
}

int isFull(Queue* q) {
    // If the next position is the front, the queue is full
    return (q->rear + 1) % q->size == q->front;
}

int getQueueSize(Queue* q) {
    // If the queue is empty, return 0
    if (q->front == -1) return 0;
    // If the front is behind the rear, return the difference
    if (q->front <= q->rear) return q->rear - q->front + 1;
    // If the front is ahead of the rear, return the difference plus the size of the queue
    return q->size - q->front + q->rear + 1;
} 

// Function to check if the queue is empty
bool isEmpty(Queue* q) {
    return q->front == -1;
}

void enqueue(Queue *q, BCB *block) {
    // If the queue is full, print an error message and return
    if (isFull(q)) {
        printf("Queue overflow\n");
        exit(-1);
    }
    // If the queue is empty, set the front to the first position
    if (q->front == -1) {
        q->front = 0;
    }
    // Add the data to the queue and move the rear pointer
    q->rear = (q->rear + 1) % q->size;
    q->items[q->rear] = *block;
}

BCB dequeue(Queue* q) {
    // If the queue is empty, print an error message and return -1
    if (isEmpty(q)) {
        printf("Queue underflow\n");
        exit(-1);
    }
    // Get the data from the front of the queue
    BCB data = q->items[q->front];
    // If the front and rear pointers are at the same position, reset them
    if (q->front == q->rear) {
        q->front = q->rear = -1;
    }
    else {
        // Otherwise, move the front pointer to the next position
        q->front = (q->front + 1) % q->size;
    }
    // Return the dequeued data
    return data;
}
