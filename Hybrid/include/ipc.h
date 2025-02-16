#ifndef IPC_H
#define IPC_H

#include <mpi.h>

#include "common.h"

void block_to_buffer(BCB* block, int **buffer);
bool buffer_to_block(int *buffer, BCB *block);
void receive_message(Message *message, int source, MPI_Request *request, int tag);
void send_message(int destination, MPI_Request *request, MessageType type, int data1, int data2, bool invalid, int tag);
void init_requests_and_messages();
void worker_receive_work(int source);
void worker_send_work(int destination, int expected_queue_size);
void worker_check_messages();
void manager_consume_message(Message *message, int source);
void manager_check_messages(); 
void wait_for_message(MPI_Request *request);

#endif