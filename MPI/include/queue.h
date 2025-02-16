#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

void initializeQueue(Queue* q, int size);
int isFull(Queue* q);
int getQueueSize(Queue* q);
bool isEmpty(Queue* q);
void enqueue(Queue *q, BCB *block);
BCB peek(Queue* q);
BCB dequeue(Queue* q);

#endif