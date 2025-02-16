#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

void initializeQueue(Queue* q, int size);
void initializeQueueArray(Queue **leaf_queues, int array_size, int queue_size);
int isFull(Queue* q);
int getQueueSize(Queue* q);
bool isEmpty(Queue* q);
void enqueue(Queue *q, BCB *block);
BCB dequeue(Queue* q);

#endif