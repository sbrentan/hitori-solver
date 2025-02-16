#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>

#include "../include/common.h"
#include "../include/board.h"
#include "../include/utils.h"
#include "../include/pruning.h"
#include "../include/queue.h"
#include "../include/validation.h"
#include "../include/backtracking.h"
#include "../include/ipc.h"

/* ------------------ GLOBAL VARIABLES ------------------ */
Board board;
Queue solution_queue;
int rank, size;

// ----- MPI variables -----
MPI_Datatype MPI_MESSAGE;
MPI_Comm PRUNING_COMM;

// ----- Backtracking variables -----
bool terminated = false;
bool is_my_solution_spaces_ended = false;
int solutions_to_skip = 0;
int total_processes_in_solution_space = 1;
int *unknown_index, *unknown_index_length, *processes_in_my_solution_space;

// ----- Worker variables -----
Message messagesqueue[MAX_MSG_SIZE];
int message_index = 0;

Message manager_message, receive_work_message, refresh_solution_space_message, finished_solution_space_message;
MPI_Request manager_request;   // Request for the workers to contact the manager
MPI_Request receive_work_request, refresh_solution_space_request; // dedicated worker-worker
int *receive_work_buffer, *send_work_buffer;

// ----- Manager variables -----
Message *worker_messages;
MPI_Request *worker_requests;   // Requests for the manager to contact the workers
WorkerStatus *worker_statuses;  // Status of each worker

/* ------------------ FUNCTION DECLARATIONS ------------------ */

void block_to_buffer(BCB* block, int **buffer) {

    /*
        Utility function to convert a block into a buffer. Needed to receive the block from MPI.
    */

    memcpy(*buffer, block->solution, board.rows_count * board.cols_count * sizeof(CellState));

    int i;
    for (i = 0; i < board.rows_count * board.cols_count; i++)
        (*buffer)[board.rows_count * board.cols_count + i] = block->solution_space_unknowns[i] ? 1 : 0;
}

bool buffer_to_block(int *buffer, BCB *block) {

    /*
        Utility function to convert a buffer to a block. Needed to send the block over MPI.
    */

    int i;
    block->solution = malloc(board.rows_count * board.cols_count * sizeof(CellState));
    block->solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool));
    
    memcpy(block->solution, buffer, board.rows_count * board.cols_count * sizeof(CellState));
    for (i = 0; i < board.rows_count * board.cols_count; i++) {
        block->solution_space_unknowns[i] = buffer[board.rows_count * board.cols_count + i] == 1;
    }

    return true;
}

void receive_message(Message *message, int source, MPI_Request *request, int tag) {
    
    if (source == rank && rank != MANAGER_RANK) {
        if (DEBUG) printf("[ERROR] Process %d tried to receive a message from itself\n", rank);
        exit(-1);
    }

    int flag = 0;
    MPI_Test(request, &flag, MPI_STATUS_IGNORE);
    
    if (!flag) {
        if (DEBUG) printf("[ERROR] Process %d tried to receive a message from process %d while the previous request is not completed (with tag %d)\n", rank, source, tag);
    } else {
        // Receive a non-blocking message from the source process
        MPI_Irecv(message, 1, MPI_MESSAGE, source, tag, MPI_COMM_WORLD, request);
    }
}

void send_message(int destination, MPI_Request *request, MessageType type, int data1, int data2, bool invalid, int tag) {

    if (destination == rank && rank != MANAGER_RANK) {
        if (DEBUG) printf("[ERROR] Process %d tried to send a message to itself\n", rank);
        exit(-1);
    }
    int flag = 0;
    MPI_Test(request, &flag, MPI_STATUS_IGNORE);
    if (!flag) {
        if (DEBUG) printf("[WARNING] Process %d tried to send a message to process %d while the previous request is not completed, waiting for it (with tag %d)\n", rank, destination, tag);
        wait_for_message(request);
        if (DEBUG) printf("[INFO] Process %d finished waiting for the previous request to complete\n", rank);
    }
    
    // Send a non-blocking message to the destination process
    messagesqueue[message_index] = (Message){type, data1, data2, invalid};
    MPI_Isend(&messagesqueue[message_index], 1, MPI_MESSAGE, destination, tag, MPI_COMM_WORLD, request);
    message_index = (message_index + 1) % MAX_MSG_SIZE;
    if (DEBUG) printf("[INFO] Process %d sent a message with tag %d to process %d with type %d, data1 %d, data2 %d and invalid %d\n", rank, tag, destination, type, data1, data2, invalid);
}

void init_requests_and_messages() {

    /*
        Commit the MPI_Datatype for the StatusMessage struct
    */

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

    /*
        Initialize all the requests and messages
    */

    if (rank == MANAGER_RANK) {
        worker_requests = (MPI_Request *) malloc(size * sizeof(MPI_Request));
        worker_messages = (Message *) malloc(size * sizeof(Message));
        worker_statuses = (WorkerStatus *) malloc(size * sizeof(WorkerStatus));
        int i;
        for (i = 0; i < size; i++) {
            worker_statuses[i].queue_size = 0;
            worker_statuses[i].processes_sharing_solution_space = 0;
            worker_statuses[i].master_process = -1;

            worker_requests[i] = MPI_REQUEST_NULL;
            receive_message(&worker_messages[i], i, &worker_requests[i], W2M_MESSAGE);
        }
    }
    manager_request = MPI_REQUEST_NULL;
    receive_work_request = MPI_REQUEST_NULL;
    refresh_solution_space_request = MPI_REQUEST_NULL;
    receive_work_buffer = (int *) malloc(board.cols_count * board.rows_count * 2 * sizeof(int));
    send_work_buffer = (int *) malloc(board.cols_count * board.rows_count * 2 * sizeof(int));
    processes_in_my_solution_space = (int *) malloc(size * sizeof(int));
    memset(processes_in_my_solution_space, -1, size * sizeof(int));
    receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);
}

void worker_receive_work(int source) {
    int flag = 0;
    
    // --- receive initial message
    receive_message(&receive_work_message, source, &receive_work_request, W2W_MESSAGE);
    wait_for_message(&receive_work_request);
    if (terminated) return;

    if (receive_work_message.invalid) {
        if (DEBUG) printf("[ERROR] Process %d received an invalid message from process while receiving work %d\n", rank, source);
        MPI_Request new_ask_for_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &new_ask_for_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
        return;
    }

    solutions_to_skip = receive_work_message.data1;
    total_processes_in_solution_space = receive_work_message.data2;
    if (DEBUG) printf("[INFO] Process %d received work from process %d with solutions to skip %d and total processes in solution space %d\n", rank, source, solutions_to_skip, total_processes_in_solution_space);

    // --- receive buffer
    MPI_Status status;
    MPI_Test(&receive_work_request, &flag, &status);
    
    if (DEBUG && (!flag || status.MPI_SOURCE != -2))
        printf("[ERROR] MPI_Test in worker RECEIVE_WORK failed with flag %d and status %d\n", flag, status.MPI_SOURCE);

    MPI_Irecv(receive_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, source, W2W_BUFFER, MPI_COMM_WORLD, &receive_work_request);
    wait_for_message(&receive_work_request);
    if (terminated) return;

    BCB block_to_receive;
    if (buffer_to_block(receive_work_buffer, &block_to_receive))
        enqueue(&solution_queue, &block_to_receive);

    // --- open refresh solution space message channel
    if (total_processes_in_solution_space > 1) {
        receive_message(&refresh_solution_space_message, source, &refresh_solution_space_request, W2W_MESSAGE * 4);
        if (DEBUG && !is_my_solution_spaces_ended)
            printf("[ERROR] Process %d received a refresh solution space message from process %d while not solution space ended\n", rank, source);
    }
    else 
        is_my_solution_spaces_ended = false;
}

void worker_send_work(int destination, int expected_queue_size) {
    
    int queue_size = getQueueSize(&solution_queue);
    bool invalid_request = terminated || is_my_solution_spaces_ended || queue_size == 0 || expected_queue_size != queue_size;
    
    if (DEBUG && invalid_request)
        printf("[ERROR] Process %d sending invalid send work request to process %d [%d, %d, %d, %d]\n", rank, destination, terminated, is_my_solution_spaces_ended, queue_size, expected_queue_size);

    BCB block_to_send;
    int solutions_to_skip_to_send = 0;
    int total_processes_in_solution_space_to_send = 0;

    if (!invalid_request) {
        if (queue_size == 1) {
            block_to_send = peek(&solution_queue);
            block_to_buffer(&block_to_send, &send_work_buffer);
            solutions_to_skip_to_send = total_processes_in_solution_space;
            
            if (DEBUG && processes_in_my_solution_space[destination] == 1)
                printf("[ERROR] Process %d found process %d already in its solution space\n", rank, destination);
            
            processes_in_my_solution_space[destination] = 1;
            solutions_to_skip = 0;

            // --- tell the workers in the same solution space to update the next solution space
            int i, count = 0;
            for (i = 0; i < size; i++) {
                if (processes_in_my_solution_space[i] == -1 || i == destination) continue;
                count++;
            }

            total_processes_in_solution_space = count + 1; 
            total_processes_in_solution_space_to_send = total_processes_in_solution_space;
            count = 0;
            for (i = 0; i < size; i++) {
                if (processes_in_my_solution_space[i] == -1 || i == destination) continue;
                MPI_Request request = MPI_REQUEST_NULL;
                send_message(i, &request, REFRESH_SOLUTION_SPACE, ++count, total_processes_in_solution_space, false, W2W_MESSAGE * 4);
            }
        }
        else if(queue_size > 1) {
            block_to_send = dequeue(&solution_queue);
            block_to_buffer(&block_to_send, &send_work_buffer);
            solutions_to_skip_to_send = 0;
            total_processes_in_solution_space_to_send = 1;
        }
    }

    // --- send initial message
    MPI_Request send_work_request = MPI_REQUEST_NULL;
    send_message(destination, &send_work_request, WORKER_SEND_WORK, solutions_to_skip_to_send, total_processes_in_solution_space_to_send, invalid_request, W2W_MESSAGE);

    // --- send buffer
    if (!invalid_request) {
        MPI_Request send_work_buffer_request;
        MPI_Isend(send_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, destination, W2W_BUFFER, MPI_COMM_WORLD, &send_work_buffer_request);
    }
}

void worker_check_messages() {
    int flag = 1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        // Test if the worker has received a message from the manager
        MPI_Test(&manager_request, &flag, &status);
        if (flag) {
            // Open a new Manager-to-Worker channel
            receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);

            if (DEBUG && status.MPI_SOURCE == -2)
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for manager message\n", rank);
            
            if (DEBUG) printf("[INFO] Process %d received a message from manager {%d}\n", rank, manager_message.type);

            /*
                Based on the received message, the worker will take the appropriate action.
            */

            if (manager_message.type == TERMINATE) {
                terminated = true;
                return;
            }
            else if (manager_message.type == SEND_WORK) {
                worker_send_work(manager_message.data1, manager_message.data2);
            }
            else if (manager_message.type == RECEIVE_WORK) {
                worker_receive_work(manager_message.data1);
            }
            else if(manager_message.type == FINISHED_SOLUTION_SPACE) {
                if (DEBUG && processes_in_my_solution_space[manager_message.data1] == -1)
                    printf("[ERROR] Process %d received an invalid FINISHED_SOLUTION_SPACE manager_message from process %d (already -1)\n", rank, status.MPI_SOURCE);
                processes_in_my_solution_space[manager_message.data1] = -1;
            }
            else if (DEBUG)
                printf("[ERROR] Process %d received an invalid manager_message type %d from manager\n", rank, manager_message.type);
        }
    }

    // When a new worker joins the solution space, the manager of the solution space need to update the total_processes_in_solution_space and
    // the solutions_to_skip, in order to coordinate the workers in the same solution space
    if (is_my_solution_spaces_ended && total_processes_in_solution_space > 1) {
        flag = 0;
        MPI_Test(&refresh_solution_space_request, &flag, &status);
        if (flag) {
            receive_message(&refresh_solution_space_message, status.MPI_SOURCE, &refresh_solution_space_request, W2W_MESSAGE * 4);
            if (refresh_solution_space_message.type == REFRESH_SOLUTION_SPACE) {
                int buffer_size = board.cols_count * board.rows_count * 2;
                int refresh_solution_space_buffer[buffer_size];
                
                if (DEBUG && status.MPI_SOURCE == -2)
                    printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for buffer solution refresh\n", rank);
                
                MPI_Irecv(refresh_solution_space_buffer, buffer_size, MPI_INT, status.MPI_SOURCE, W2W_BUFFER, MPI_COMM_WORLD, &refresh_solution_space_request);
                wait_for_message(&refresh_solution_space_request);
                if (terminated) return;

                BCB new_block;
                initializeQueue(&solution_queue, SOLUTION_SPACES);
                if (buffer_to_block(refresh_solution_space_buffer, &new_block))
                    enqueue(&solution_queue, &new_block);
            } else if (DEBUG)
                printf("[ERROR] Process %d received an invalid message type %d from process %d (instead of refresh)\n", rank, refresh_solution_space_message.type, status.MPI_SOURCE);
        }
    }
}

void manager_consume_message(Message *message, int source) {

    /*
        Based on the message type, the manager will take the appropriate action.

        TERMINATE: The manager will send a TERMINATE message to all the workers
        STATUS_UPDATE: The manager will update the status of the worker with queue size and processes sharing solution space
        ASK_FOR_WORK: The manager will assign work to the worker with the smallest queue size
    */

    if (DEBUG) printf("[INFO] Process %d (manager) received a message from process %d {%d}\n", rank, source, message->type);
    int i; //sender_id;
    MPI_Request send_worker_request = MPI_REQUEST_NULL;
    if (message->type == TERMINATE) {
        // Send termination message to all the workers, except for itself
        for (i = 0; i < size; i++) {
            if (i == MANAGER_RANK || i == message->data1) continue;
            send_message(i, &send_worker_request, TERMINATE, source, -1, false, M2W_MESSAGE);
        }
        terminated = true;
    }
    else if (message->type == STATUS_UPDATE) {
        // Update the statues of that worker
        worker_statuses[source].queue_size = message->data1;
        worker_statuses[source].processes_sharing_solution_space = message->data2;
    }
    else if (message->type == ASK_FOR_WORK) {
        
        if(terminated) {
            send_message(source, &send_worker_request, TERMINATE, rank, -1, false, M2W_MESSAGE);
            return;
        }

        /*
            Find the target worker for the work assignment, based on the minimum queue size and processes sharing solution space.
        */

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

        // If the worker cannot be found, then send a TERMINATE message to the source worker
        if (target_worker == -1) {
            if (DEBUG) printf("[INFO] Process %d (manager) could not find a target worker to assign work for process %d\n", rank, source);
            send_message(source, &send_worker_request, TERMINATE, rank, -1, false, M2W_MESSAGE);
        } else {

            // Otherwise, notify both the source and the target workers about the work assignment
            if (DEBUG) printf("[INFO] Process %d (manager) assigned work for process %d to worker %d\n", rank, source, target_worker);
            MPI_Request send_work_request = MPI_REQUEST_NULL;
            send_message(target_worker, &send_work_request, SEND_WORK, source, min_queue_size, false, M2W_MESSAGE);
            send_message(source, &send_worker_request, RECEIVE_WORK, target_worker, -1, false, M2W_MESSAGE);
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
                    if (DEBUG) printf("[ERROR] Process %d (manager) got invalid master process %d for target worker %d\n", rank, worker_statuses[target_worker].master_process, target_worker);
                if (worker_statuses[source].master_process != -1)
                    if (DEBUG) printf("[ERROR] Process %d (manager) got invalid master process %d for source worker %d\n", rank, worker_statuses[source].master_process, source);
                worker_statuses[source].master_process = -1;
                if (worker_statuses[target_worker].processes_sharing_solution_space != 1)
                    if (DEBUG) printf("[ERROR] Process %d (manager) got invalid processes sharing solution space %d for target worker %d\n", rank, worker_statuses[target_worker].processes_sharing_solution_space, target_worker);
            } else if (DEBUG) 
                printf("[ERROR] Process %d (manager) got invalid queue size %d for target worker %d\n", rank, worker_statuses[target_worker].queue_size, target_worker);
        }
    } else if (DEBUG) 
        printf("[ERROR] Process %d (manager) received an invalid message type %d from process %d\n", rank, message->type, source);
}

void manager_check_messages() {
    int flag = 1, sender_id = -1;
    MPI_Status status;
    while(flag) {
        flag = 0;

        // Test if the manager has received a message from any worker (including itself)
        MPI_Testany(size, worker_requests, &sender_id, &flag, &status);

        if (flag) {
            if (DEBUG && status.MPI_SOURCE == -2) {
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE (SHOULD NOT HAPPEN)\n", rank);
                continue;
            }

            if (DEBUG && status.MPI_SOURCE != sender_id)
                printf("[ERROR] Process %d got error in mapping between status.MPI_SOURCE %d and sender_id %d, mapping: %d\n", rank, status.MPI_SOURCE, sender_id, status.MPI_SOURCE);

            // open a new message channel
            receive_message(&worker_messages[sender_id], status.MPI_SOURCE, &worker_requests[sender_id], W2M_MESSAGE);

            // consume the actual message
            manager_consume_message(&worker_messages[sender_id], status.MPI_SOURCE);
        }
    }
}

void wait_for_message(MPI_Request *request) {
    
    /*
        Wait asynchronously for the message to be completed.
    */
    
    int flag = 0;
    while(!flag && !terminated) {
        MPI_Test(request, &flag, MPI_STATUS_IGNORE);
        if (!flag && rank == MANAGER_RANK) manager_check_messages();
        if (!flag) worker_check_messages();
    }
}

/* ------------------ MAIN ------------------ */

bool hitori_mpi_solution() {

    int i, count = 0;
    int my_solution_spaces[SOLUTION_SPACES];
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));
    
    /*
        Assign the solution spaces to the processes
    */

    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (i % size == rank) {
            my_solution_spaces[count++] = i;
        }
        if (rank == MANAGER_RANK) {
            worker_statuses[i % size].queue_size++;
            worker_statuses[i % size].processes_sharing_solution_space = 1;
        }
    }

    /*
        Start building the intial solution spaces
    */

    bool leaf_found = false;
    BCB blocks[SOLUTION_SPACES];

    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (my_solution_spaces[i] == -1) break;
        
        init_solution_space(board, &blocks[i], my_solution_spaces[i], &unknown_index);
        leaf_found = build_leaf(board, &blocks[i], 0, 0, &unknown_index, &unknown_index_length, &total_processes_in_solution_space, &solutions_to_skip);
        
        // check if the leaf is found
        if (leaf_found) {

            // check if the leaf is a solution
            if (check_hitori_conditions(board, &blocks[i])) {
                // if it is a solution, set the termination flag and communicate to the manager
                terminated = true;
                memcpy(board.solution, blocks[i].solution, board.rows_count * board.cols_count * sizeof(CellState));
                MPI_Request terminate_message_request = MPI_REQUEST_NULL;
                send_message(MANAGER_RANK, &terminate_message_request, TERMINATE, rank, -1, false, W2M_MESSAGE);

                // if the solver is the manager, then don't exit and finish consuming all the messages
                if (rank == MANAGER_RANK) manager_check_messages();
                return true;
            } else {
                // if it is not a solution, add it to the solution queue to be processed later
                enqueue(&solution_queue, &blocks[i]);
                count--;
            }
        } else {
            if(DEBUG) printf("Processor %d failed to find a leaf\n", rank);
        }
    }

    /*
        Send the initial statuses to the manager. If the queue is not empty, send a status update message.
        Otherwise, ask for work.
    */
    
    int queue_size = getQueueSize(&solution_queue);
    if (queue_size > 0 && count > 0) {
        MPI_Request status_update_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size, 1, false, W2M_MESSAGE);
    } else if (queue_size == 0) {
        is_my_solution_spaces_ended = true;
        MPI_Request ask_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
    }

    /*
        Start the backtracking algorithm.
    */

    while(!terminated) {

        // Check the messages for both manager and workers. Remember that the manager is also a worker.
        if (rank == MANAGER_RANK) manager_check_messages();
        worker_check_messages(&solution_queue);

        if (!terminated) {
            queue_size = getQueueSize(&solution_queue);
            if (queue_size > 0) {

                // Dequeue the block and process it
                BCB current_solution = dequeue(&solution_queue);
                leaf_found = next_leaf(board, &current_solution, &unknown_index, &unknown_index_length, &total_processes_in_solution_space, &solutions_to_skip);

                // If the block is a valid leaf, check if it is a solution
                if (leaf_found) {
                    if (check_hitori_conditions(board, &current_solution)) {
                        // if it is a solution, set the termination flag and communicate to the manager
                        terminated = true;
                        memcpy(board.solution, current_solution.solution, board.rows_count * board.cols_count * sizeof(CellState));
                        MPI_Request terminate_message_request = MPI_REQUEST_NULL;
                        send_message(MANAGER_RANK, &terminate_message_request, TERMINATE, rank, -1, false, W2M_MESSAGE);

                        // if the solver is the manager, then don't exit and finish consuming all the messages
                        if (rank == MANAGER_RANK) manager_check_messages();
                        return true;
                    } else 
                        enqueue(&solution_queue, &current_solution);
                } else {
                    // Notify the current status to the manager if the queue is not empty
                    if (queue_size > 1) {
                        MPI_Request status_update_request = MPI_REQUEST_NULL;
                        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size - 1, 1, false, W2M_MESSAGE);
                    } else if (queue_size == 1) {
                        // If the queue is now empty, ask for work
                        is_my_solution_spaces_ended = true;
                        if (DEBUG) printf("[%d] Processor is asking for work\n", rank);
                        MPI_Request ask_work_request = MPI_REQUEST_NULL;
                        send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
                    }
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

    /*
        Read the board from the input file
    */

    if (rank == MANAGER_RANK) read_board(&board, argv[1]);
    
    /*
        Share the board with all the processes
    */
    
    mpi_share_board(&board, rank);

    /*
        Print the initial board
    */

    if (DEBUG && rank == MANAGER_RANK) print_board("Initial", board, BOARD);

    /*
        Apply the basic hitori pruning techniques to the board.
    */

    int min_workers = (size < PRUNING_WORKERS) ? size : PRUNING_WORKERS;
    int color = rank < min_workers ? 1 : MPI_UNDEFINED;
    MPI_Comm_split(MPI_COMM_WORLD, color, rank, &PRUNING_COMM);

    double pruning_start_time = MPI_Wtime();
    if(PRUNING_COMM != MPI_COMM_NULL) {

        Board (*techniques[])(Board, int, int, MPI_Comm) = {
            mpi_uniqueness_rule,
            mpi_sandwich_rules,
            mpi_pair_isolation,
            mpi_flanked_isolation,
            mpi_corner_cases
        };
        int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

        board = techniques[0](board, rank, min_workers, PRUNING_COMM);
        
        int i;
        for (i = 1; i < num_techniques; i++)
            board = combine_boards(board, techniques[i](board, rank, min_workers, PRUNING_COMM), false, rank, "Partial", PRUNING_COMM);
        
        /*
            Repeat the whiting and blacking pruning techniques until the solution doesn't change
        */

        while (true) {

            Board white_solution = mpi_set_white(board, rank, min_workers, PRUNING_COMM);
            Board black_solution = mpi_set_black(board, rank, min_workers, PRUNING_COMM);

            Board partial = combine_boards(board, white_solution, false, rank, "Partial", PRUNING_COMM);
            Board new_solution = combine_boards(partial, black_solution, false, rank, "Partial", PRUNING_COMM);

            if(!is_board_solution_equal(board, new_solution)) 
                board = new_solution;
            else 
                break;
        }
    }
    double pruning_end_time = MPI_Wtime();

    if (PRUNING_COMM != MPI_COMM_NULL) MPI_Comm_free(&PRUNING_COMM);
    
    if (DEBUG && rank == MANAGER_RANK) print_board("Pruned", board, SOLUTION);

    /*
        Share the pruned board with all the processes
    */

    mpi_share_board(&board, rank);

    /*
        Initialize the backtracking variables
    */
    
    initializeQueue(&solution_queue, SOLUTION_SPACES);
    init_requests_and_messages();

    /*
        Compute the unknown cells indexes
    */

    compute_unknowns(board, &unknown_index, &unknown_index_length);
    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    double recursive_start_time = MPI_Wtime();
    bool solution_found = hitori_mpi_solution();
    double recursive_end_time = MPI_Wtime();

    MPI_Barrier(MPI_COMM_WORLD);
    
    /*
        Print all the times
    */

    if (rank == MANAGER_RANK) printf("[%d] Time for pruning part: %f\n", rank, pruning_end_time - pruning_start_time);
    
    if (rank == MANAGER_RANK) printf("[%d] Time for recursive part: %f\n", rank, recursive_end_time - recursive_start_time);    
    
    if (rank == MANAGER_RANK) printf("[%d] Total execution time: %f\n", rank, recursive_end_time - pruning_start_time);

    MPI_Barrier(MPI_COMM_WORLD);

    /*
        Write the final solution to the output file
    */

    if (solution_found) {
        write_solution(board);
        char formatted_string[MAX_BUFFER_SIZE];
        snprintf(formatted_string, MAX_BUFFER_SIZE, "Solution found by process %d", rank);
        print_board(formatted_string, board, SOLUTION);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    /*
        Free the memory and finalize the MPI environment
    */

    free_memory((int *[]){
        unknown_index, 
        unknown_index_length, 
        processes_in_my_solution_space, 
        receive_work_buffer, 
        send_work_buffer
    });
    
    free(worker_messages);
    free(worker_requests);
    free(worker_statuses);

    MPI_Type_free(&MPI_MESSAGE);
    MPI_Finalize();

    return 0;
}