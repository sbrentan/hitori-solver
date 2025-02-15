#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <omp.h>

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
Queue *leaf_queues;
int rank, size;

// ----- Backtracking variables -----
bool terminated = false;
bool is_my_solution_spaces_ended = false;
int solutions_to_skip = 0; // TODO: remove?
int total_processes_in_solution_space = 1; // TODO: remove?
bool process_is_solver = false;

int process_solution_spaces, starting_solutions_to_skip;
int *total_processes_in_solution_spaces;
int *unknown_index, *unknown_index_length, *processes_in_my_solution_space;
BCB *received_block = NULL;

// ----- Common variables -----
MPI_Datatype MPI_MESSAGE;

// ----- Worker variables -----
Message manager_message, receive_work_message, refresh_solution_space_message, finished_solution_space_message;
MPI_Request manager_request;   // Request for the workers to contact the manager
MPI_Request receive_work_request, refresh_solution_space_request, finished_solution_space_request; // dedicated worker-worker
int *receive_work_buffer, *send_work_buffer;

bool send_status_update_message = false;
bool send_terminate_message = false;
// int not_ended_solution_spaces;

// ----- Manager variables -----
Message *worker_messages;
MPI_Request *worker_requests;   // Requests for the manager to contact the workers
WorkerStatus *worker_statuses;  // Status of each worker


/* ------------------ FUNCTION DECLARATIONS ------------------ */

void block_to_buffer(BCB* block, int **buffer) {
    memcpy(*buffer, block->solution, board.rows_count * board.cols_count * sizeof(CellState));

    int i;
    for (i = 0; i < board.rows_count * board.cols_count; i++)
        (*buffer)[board.rows_count * board.cols_count + i] = block->solution_space_unknowns[i] ? 1 : 0;
}

bool buffer_to_block(int *buffer, BCB *block) {

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
    if (size == 1) return;
    
    if (omp_get_thread_num() != MANAGER_THREAD) {
        printf("[ERROR] Process %d got invalid thread number %d while receiving\n", rank, omp_get_thread_num());
        return;
    }

    // print the message
    printf("[RECEIVE MESSAGE] Message is %d, %d, %d, %d\n", message->type, message->data1, message->data2, message->invalid);

    if (source == rank && rank != MANAGER_RANK) {
        printf("[ERROR] Process %d tried to receive a message from itself\n", rank);
        exit(-1);
    }
    int flag = 0;
    MPI_Status status;
    MPI_Test(request, &flag, &status);
    if (!flag)
        printf("[ERROR] Process %d tried to receive a message from process %d while the previous request is not completed (with tag %d)\n", rank, source, tag);
    else {
        // if(status.MPI_SOURCE == -2)
        //     printf("[ERROR] Process %d got -2 in status.MPI_SOURCE\n", rank);
        MPI_Irecv(message, 1, MPI_MESSAGE, source, tag, MPI_COMM_WORLD, request);
    }
}

void send_message(int destination, MPI_Request *request, MessageType type, int data1, int data2, bool invalid, int tag) {
    if (size == 1) return;
    if (omp_get_thread_num() != MANAGER_THREAD) {
        printf("[ERROR] Process %d got invalid thread number %d while sending\n", rank, omp_get_thread_num());
        return;
    }
    if (destination == rank && rank != MANAGER_RANK) {
        printf("[ERROR] Process %d tried to send a message to itself\n", rank);
        exit(-1);
    }
    int flag = 0;
    MPI_Test(request, &flag, MPI_STATUS_IGNORE);
    if (!flag) {
        printf("[WARNING] Process %d tried to send a message to process %d while the previous request is not completed, waiting for it (with tag %d)\n", rank, destination, tag);
        // MPI_Wait(request, MPI_STATUS_IGNORE);
        wait_for_message(request);
        printf("[INFO] Process %d finished waiting for the previous request to complete\n", rank);
    }
    Message message = {type, data1, data2, invalid};
    MPI_Isend(&message, 1, MPI_MESSAGE, destination, tag, MPI_COMM_WORLD, request);
    printf("[INFO] Process %d sent a message with tag %d to process %d with type %d, data1 %d, data2 %d and invalid %d\n", rank, tag, destination, type, data1, data2, invalid);
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
        printf("[%d] Allocing %d requests and messages\n", rank, size);
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
    // finished_solution_space_request = MPI_REQUEST_NULL;
    receive_work_buffer = (int *) malloc(board.cols_count * board.rows_count * 2 * sizeof(int));
    send_work_buffer = (int *) malloc(board.cols_count * board.rows_count * 2 * sizeof(int));
    processes_in_my_solution_space = (int *) malloc(size * sizeof(int));
    memset(processes_in_my_solution_space, -1, size * sizeof(int));
    receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);
}

void worker_receive_work(int source) {
    if (size == 1) return;
    int flag = 0;
    
    // --- receive initial message
    receive_message(&receive_work_message, source, &receive_work_request, W2W_MESSAGE);
    wait_for_message(&receive_work_request);
    if (terminated) return;

    if (receive_work_message.invalid) {
        printf("[ERROR] Process %d received an invalid message from process while receiving work %d\n", rank, source);
        MPI_Request new_ask_for_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &new_ask_for_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
        return;
    }

    starting_solutions_to_skip = receive_work_message.data1;
    total_processes_in_solution_spaces[0] = receive_work_message.data2;
    printf("[INFO] Process %d received work from process %d with solutions to skip %d and total processes in solution space %d\n", rank, source, solutions_to_skip, total_processes_in_solution_space);

    // --- receive buffer
    MPI_Status status;
    MPI_Test(&receive_work_request, &flag, &status);
    if (!flag || status.MPI_SOURCE != -2)
        printf("[ERROR] MPI_Test in worker RECEIVE_WORK failed with flag %d and status %d\n", flag, status.MPI_SOURCE);
    MPI_Irecv(receive_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, source, W2W_BUFFER, MPI_COMM_WORLD, &receive_work_request);
    wait_for_message(&receive_work_request);
    if (terminated) return;

    BCB block_to_receive;
    if (buffer_to_block(receive_work_buffer, &block_to_receive)) {
        received_block = &block_to_receive;
        #pragma omp atomic write
        terminated = true;
    }

    // --- open refresh solution space message channel
    if (total_processes_in_solution_spaces[0] > 1) {
        receive_message(&refresh_solution_space_message, source, &refresh_solution_space_request, W2W_MESSAGE * 4);
        if (!is_my_solution_spaces_ended)
            printf("[ERROR] Process %d received a refresh solution space message from process %d while not solution space ended\n", rank, source);
    }
    else {
        printf("[WARNING] Should not happen in hybrid approach");
        is_my_solution_spaces_ended = false;
    }
}

void worker_send_work(int destination, int expected_queue_size) {
    if (size == 1) return;
    
    // int queue_size = getQueueSize(&solution_queue);
    // TODO: change
    return;
    
    int i;
    int not_ended_solution_spaces = 0;
    int solution_space_to_send = -1;
    for (i = 0; i < process_solution_spaces; i++) {
        if (total_processes_in_solution_spaces[i] > 0) {
            if (solution_space_to_send == -1 || total_processes_in_solution_spaces[i] < total_processes_in_solution_spaces[solution_space_to_send])
                solution_space_to_send = i;
            not_ended_solution_spaces++;
        }
    }
    int max_threads = omp_get_max_threads();

    bool invalid_request = terminated || is_my_solution_spaces_ended || not_ended_solution_spaces == 0 || expected_queue_size != not_ended_solution_spaces;
    if (invalid_request)
        printf("[ERROR] Process %d sending invalid send work request to process %d [%d, %d, %d, %d]\n", rank, destination, terminated, is_my_solution_spaces_ended, not_ended_solution_spaces, expected_queue_size);

    BCB block_to_send;
    int solutions_to_skip_to_send = 0;
    int total_processes_in_solution_space_to_send = 0;

    if (!invalid_request) {
        if (not_ended_solution_spaces == 1) {
            // TODO: change
            block_to_send = peek(&solution_queue);
            block_to_buffer(&block_to_send, &send_work_buffer);
            solutions_to_skip_to_send = total_processes_in_solution_space;
            if (processes_in_my_solution_space[destination] == 1)
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
                printf("[%d] Processes in my solution space %d, %d\n", rank, i, processes_in_my_solution_space[i]);
                MPI_Request request = MPI_REQUEST_NULL;
                send_message(i, &request, REFRESH_SOLUTION_SPACE, ++count, total_processes_in_solution_space, false, W2W_MESSAGE * 4);
            }
        }
        else if(not_ended_solution_spaces > 1) {
            // TODO: change
            block_to_send = dequeue(&solution_queue);
            block_to_buffer(&block_to_send, &send_work_buffer);
            solutions_to_skip_to_send = 0;
            total_processes_in_solution_space_to_send = 1;
        }
    }
    // printf("[%d] Sending initial message\n", rank);
    // --- send initial message
    MPI_Request send_work_request = MPI_REQUEST_NULL;
    send_message(destination, &send_work_request, WORKER_SEND_WORK, solutions_to_skip_to_send, total_processes_in_solution_space_to_send, invalid_request, W2W_MESSAGE);
    // printf("[%d] Sent initial message\n", rank);

    // --- send buffer
    if (!invalid_request) {
        MPI_Request send_work_buffer_request;
        MPI_Isend(send_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, destination, W2W_BUFFER, MPI_COMM_WORLD, &send_work_buffer_request);
    }
}

void worker_check_messages() {
    if (size == 1) return;
    if (omp_get_thread_num() != MANAGER_THREAD) {
        printf("[ERROR] Process %d got invalid thread number %d\n", rank, omp_get_thread_num());
        return;
    }

    if (send_terminate_message) {
        send_terminate_message = false;
        MPI_Request terminate_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &terminate_request, TERMINATE, -1, -1, false, W2M_MESSAGE);
        // #pragma omp atomic write
        // terminated = true;
    }
    // else if(send_status_update_message) {
    //     send_status_update_message = false;
    //     MPI_Request status_update_request = MPI_REQUEST_NULL;
    //     int i;
    //     int not_ended_solution_spaces = 0;
    //     for (i = 0; i < process_solution_spaces; i++) {
    //         if (total_processes_in_solution_spaces[i] > 0)
    //             not_ended_solution_spaces++;
    //     }
    //     int max_threads = omp_get_max_threads();
        
    //     // TODO: check max_threads as data 2
    //     if (not_ended_solution_spaces > 0)
    //         send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, not_ended_solution_spaces, max_threads, false, W2M_MESSAGE);
    //     else {
    //         is_my_solution_spaces_ended = true;
    //         printf("Processor %d is asking for work\n", rank);
    //         MPI_Request ask_work_request = MPI_REQUEST_NULL;
    //         send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
    //     }
    // }

    int flag = 1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        // printf("[%d] Testing\n", rank);
        // #pragma omp critical
        MPI_Test(&manager_request, &flag, &status);
        // printf("[%d] Finished testing\n", rank);
        if (flag) {
            receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);

            if (status.MPI_SOURCE == -2)
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for manager message\n", rank);
            
            printf("[INFO] Process %d received a message from manager {%d}\n", rank, manager_message.type);
            if (manager_message.type == TERMINATE) {
                #pragma omp atomic write
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
                if (processes_in_my_solution_space[manager_message.data1] == -1)
                    printf("[ERROR] Process %d received an invalid FINISHED_SOLUTION_SPACE message from process %d (already -1)\n", rank, status.MPI_SOURCE);
                processes_in_my_solution_space[manager_message.data1] = -1;
            }
            else {
                printf("[ERROR] Process %d received an invalid message type %d from manager\n", rank, manager_message.type);
                // return &manager_message;
                #pragma omp atomic write
                terminated = true;
                return;
            }
        }
    }

    if (is_my_solution_spaces_ended && total_processes_in_solution_space > 1) {
        // TODO: check if message is from correct master
        flag = 0;
        MPI_Test(&refresh_solution_space_request, &flag, &status);
        if (flag) {
            receive_message(&refresh_solution_space_message, status.MPI_SOURCE, &refresh_solution_space_request, W2W_MESSAGE * 4);
            if (refresh_solution_space_message.type == REFRESH_SOLUTION_SPACE) {
                int buffer_size = board.cols_count * board.rows_count * 2;
                int refresh_solution_space_buffer[buffer_size];
                if (status.MPI_SOURCE == -2)
                    printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for buffer solution refresh\n", rank);
                MPI_Irecv(refresh_solution_space_buffer, buffer_size, MPI_INT, status.MPI_SOURCE, W2W_BUFFER, MPI_COMM_WORLD, &refresh_solution_space_request);
                wait_for_message(&refresh_solution_space_request);
                if (terminated) return;

                BCB new_block;
                // TODO: change
                initializeQueue(&solution_queue, SOLUTION_SPACES);
                if (buffer_to_block(refresh_solution_space_buffer, &new_block))
                    enqueue(&solution_queue, &new_block);
            }
            else
                printf("[ERROR] Process %d received an invalid message type %d from process %d (instead of refresh)\n", rank, refresh_solution_space_message.type, status.MPI_SOURCE);
        }
    }
}

MPI_Request send_worker_request = MPI_REQUEST_NULL;
void manager_consume_message(Message *message, int source) {
    if (size == 1) return;
    printf("[INFO] Process %d (manager) received a message from process %d {%d}\n", rank, source, message->type);
    int i; //sender_id;
    
    if (message->type == STATUS_UPDATE) {
        worker_statuses[source].queue_size = message->data1;
        worker_statuses[source].processes_sharing_solution_space = message->data2;
    }
    else if (message->type == ASK_FOR_WORK) {
        
        if(terminated) {
            send_message(source, &send_worker_request, TERMINATE, rank, -1, false, M2W_MESSAGE);
            return;
        }

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
            send_message(source, &send_worker_request, TERMINATE, rank, -1, false, M2W_MESSAGE);
        } else {
            printf("[INFO] Process %d (manager) assigned work for process %d to worker %d\n", rank, source, target_worker);
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
                    printf("[ERROR] Process %d (manager) got invalid master process %d for target worker %d\n", rank, worker_statuses[target_worker].master_process, target_worker);
                if (worker_statuses[source].master_process != -1)
                    printf("[ERROR] Process %d (manager) got invalid master process %d for source worker %d\n", rank, worker_statuses[source].master_process, source);
                worker_statuses[source].master_process = -1;
                if (worker_statuses[target_worker].processes_sharing_solution_space != 1)
                    printf("[ERROR] Process %d (manager) got invalid processes sharing solution space %d for target worker %d\n", rank, worker_statuses[target_worker].processes_sharing_solution_space, target_worker);
            } else
                printf("[ERROR] Process %d (manager) got invalid queue size %d for target worker %d\n", rank, worker_statuses[target_worker].queue_size, target_worker);
        }
    }
    else {
        // printf("[ERROR] Process %d (manager) received an invalid message type %d from process %d\n", rank, message->type, source);
        for (i = 0; i < size; i++) {
            if (i == MANAGER_RANK) continue;
            send_message(i, &send_worker_request, TERMINATE, source, -1, false, M2W_MESSAGE);
        }
        wait_for_message(&send_worker_request);
        terminated = true;
    }
}

void manager_check_messages() {
    if (size == 1) return;
    if (omp_get_thread_num() != MANAGER_THREAD) {
        printf("[ERROR] Manager %d got invalid thread number %d\n", rank, omp_get_thread_num());
        return;
    }
    int flag = 1, sender_id = -1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        #pragma omp critical
        MPI_Testany(size, worker_requests, &sender_id, &flag, &status);

        if (flag) {
            if (status.MPI_SOURCE == -2) {
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE (SHOULD NOT HAPPEN)\n", rank);
                continue;
            }

            if (status.MPI_SOURCE != sender_id)
                printf("[ERROR] Process %d got error in mapping between status.MPI_SOURCE %d and sender_id %d, mapping: %d\n", rank, status.MPI_SOURCE, sender_id, status.MPI_SOURCE);

            receive_message(&worker_messages[sender_id], status.MPI_SOURCE, &worker_requests[sender_id], W2M_MESSAGE);
            manager_consume_message(&worker_messages[sender_id], status.MPI_SOURCE);
        }
    }
}

void wait_for_message(MPI_Request *request) {
    if (size == 1) return;
    int flag = 0;
    while(!flag && !terminated) {
        MPI_Test(request, &flag, MPI_STATUS_IGNORE);
        if (!flag && rank == MANAGER_RANK) manager_check_messages();
        if (!flag) worker_check_messages();
    }
}



void task_build_solution_space(int solution_space_id){
    int thread_num = omp_get_thread_num();
    printf("Building solution space %d\n", solution_space_id);
    BCB block = {
        .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
        .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
    };
    init_solution_space(board, &block, solution_space_id, &unknown_index);
    int threads_in_solution_space = 1;
    int solutions_to_skip = 0;
    bool leaf_found = build_leaf(board, &block, 0, 0, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
    if (leaf_found) {
        if (DEBUG) printf("[%d - %d] Leaf found\n", rank, thread_num);
        // double dfs_time = 0;
        // double conditions_time = 0;
        bool solution_found = check_hitori_conditions(board, &block);
        if (solution_found) {
            #pragma omp critical
            {
                terminated = true;
                memcpy(board.solution, block.solution, board.rows_count * board.cols_count * sizeof(CellState));
            }
            double time = omp_get_wtime();
            if (DEBUG) printf("[%d - %d] Solution found %f\n", rank, thread_num, time);
            fflush(stdout);
        } else {
            #pragma omp critical
            enqueue(&solution_queue, &block);
        }
    } else {
        if (DEBUG) printf("[%d - %d] Leaf not found\n", rank, thread_num);
    }
}

void task_find_solution_final(int thread_id, int threads_in_solution_space, int solutions_to_skip, int blocks_per_thread) {
    Queue local_queue;
    initializeQueue(&local_queue, blocks_per_thread);

    // TODO: create local board
    printf("[%d] Creating local queue %f\n", thread_id, omp_get_wtime());
    
    int i;
    #pragma omp critical
    {
        for(i = 0; i < blocks_per_thread; i++) {
            BCB block = dequeue(&leaf_queues[thread_id]);
            enqueue(&local_queue, &block);
        }
    }

    int queue_size = getQueueSize(&local_queue);
    printf("[%d] Local queue size: %d %f\n", thread_id, queue_size, omp_get_wtime());
    fflush(stdout);
    if (queue_size > 1 && solutions_to_skip > 0) {
        printf("[%d] ERROR: More than one block in local queue\n", thread_id);
        exit(-1);
    }

    // int queue_size;
    while(!terminated) {
        
        if (omp_get_thread_num() == MANAGER_THREAD) {
            worker_check_messages();
            if (rank == MANAGER_RANK) manager_check_messages();
        }

        queue_size = getQueueSize(&local_queue);
        if (terminated) {
            if (DEBUG) printf("[%d] Terminated\n", thread_id);
            fflush(stdout);
            break;
        }

        if (queue_size > 0) {
            BCB current = dequeue(&local_queue);
            
            bool leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
            if (leaf_found) {
                bool solution_found = check_hitori_conditions(board, &current);
                if (solution_found) {
                    
                    send_terminate_message = true;
                    process_is_solver = true;
                    memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));

                    printf("[%d] [%d] Solution found %f\n", rank, thread_id, omp_get_wtime());
                    #pragma omp atomic write
                    terminated = true;
                    fflush(stdout);

                    if (omp_get_thread_num() == MANAGER_THREAD) continue;
                    else break;
                    
                    // if (omp_get_thread_num() != MANAGER_THREAD) {
                    //     printf("[%d] Exiting\n", thread_id);
                    //     initializeQueue(&local_queue, blocks_per_thread);
                    //     continue;
                    // }
                } else {
                    if (DEBUG) printf("[%d] [%d] Enqueueing block\n", rank,thread_id);
                    enqueue(&local_queue, &current);
                }
            } else {

                #pragma omp critical
                {
                    total_processes_in_solution_spaces[thread_id % process_solution_spaces] -= 1;
                    if (total_processes_in_solution_spaces[thread_id % process_solution_spaces] == 0) {
                        send_status_update_message = true;
                    }
                }
                
                if (DEBUG) printf("[%d] One solution space ended\n", thread_id);
                fflush(stdout);
            }
        } else {
            printf("[%d] Local queue is empty\n", thread_id);
            fflush(stdout);
            break;
        }
    }
    printf("[%d] Exiting %f\n", thread_id, omp_get_wtime());
}

/* ------------------ MAIN ------------------ */

bool hitori_mpi_solution() {
    
    int max_threads = omp_get_max_threads();

    printf("Start time: %f\n", omp_get_wtime());

    int i, j, count = 0;
    // int solution_space_manager[SOLUTION_SPACES];
    int my_solution_spaces[SOLUTION_SPACES];
    
    // memset(solution_space_manager, -1, SOLUTION_SPACES * sizeof(int));
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));
    
    int starting_threads_in_solution_space = max_threads;
    for (i = 0; i < SOLUTION_SPACES; i++) {
        // solution_space_manager[i] = i % size;
        if (i % size == rank) {
            my_solution_spaces[count++] = i;
            if (size > SOLUTION_SPACES && i + SOLUTION_SPACES < size)
                starting_threads_in_solution_space += max_threads;
        }
        if (rank == MANAGER_RANK) {
            worker_statuses[i % size].queue_size++;
            worker_statuses[i % size].processes_sharing_solution_space = max_threads;
        }
    }

    starting_solutions_to_skip = 0;

    if (size > SOLUTION_SPACES * 2) {
        printf("[ERROR] Max threads is greater than 2 * SOLUTION_SPACES\n");
        return false;
    }

    for (i = SOLUTION_SPACES; i < size; i++) {
        if (i % size == rank) {
            my_solution_spaces[count++] = i % SOLUTION_SPACES;
            starting_solutions_to_skip = max_threads;
            starting_threads_in_solution_space += max_threads;
        }
        // if (rank == MANAGER_RANK) {
        //     worker_statuses[i % size].queue_size++;
        //     worker_statuses[i % size].processes_sharing_solution_space = max_threads;
        // }
    }

    if (DEBUG) printf("Processor %d has %d solution spaces with %d starting solutions to skip and %d starting threads in solution space\n", rank, count, starting_solutions_to_skip, starting_threads_in_solution_space);
    fflush(stdout);

    // print my solution spaces
    for (i = 0; i < count; i++) {
        printf("Processor %d has solution space %d\n", rank, my_solution_spaces[i]);
    }

    // return false;

    
    #pragma omp parallel
    {
        // TODO: decide whether to change allocation of variables
        #pragma omp single
        {
            for (i = 0; i < count; i++) {
                #pragma omp task firstprivate(i, my_solution_spaces)
                task_build_solution_space(my_solution_spaces[i]);
            }
        }
    }
    
    if (DEBUG) printf("Processor %d finished finding solution: %d\n", rank, getQueueSize(&solution_queue));
    
    if (terminated) {
        if (DEBUG) printf("Processor %d is about to terminate\n", rank);
        fflush(stdout);
        
        MPI_Request terminate_message_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &terminate_message_request, TERMINATE, rank, -1, false, W2M_MESSAGE);

        if (rank == MANAGER_RANK) manager_check_messages();
        return true;
    }
    
    // TODO: check numbers when threads > SOLUTION SPACES and status update message
    int queue_size = getQueueSize(&solution_queue);
    if (queue_size > 0 && queue_size < count) {
        if (DEBUG) printf("Processor %d is sending status update\n", rank);
        MPI_Request status_update_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size, 1, false, W2M_MESSAGE);
    } else if (queue_size == 0) {
        if (DEBUG) printf("Processor %d is asking for work\n", rank);
        is_my_solution_spaces_ended = true;
        MPI_Request ask_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
    }

    printf("Time after building solution spaces: %f\n", omp_get_wtime());
    
    // while(!terminated)
    // {
    #pragma omp parallel
    {
        #pragma omp single
        {
            // not_ended_solution_spaces = queue_size;
            process_solution_spaces = queue_size;
            total_processes_in_solution_spaces = calloc(process_solution_spaces, sizeof(int));

            if (process_solution_spaces > 1 && starting_solutions_to_skip != 0) {
                printf("[ERROR] Starting solutions to skip is not 0\n");
                fflush(stdout);
            }

            if (process_solution_spaces > 0) {
                int count = 0;
                for (i = 0; i < max_threads; i++) {
                    int blocks_per_thread = process_solution_spaces / max_threads;
                    if (process_solution_spaces % max_threads > i)
                        blocks_per_thread++;
                    blocks_per_thread = blocks_per_thread < 1 ? 1 : blocks_per_thread;

                    int threads_per_block = max_threads / process_solution_spaces;
                    if (max_threads % process_solution_spaces > i)
                        threads_per_block++;
                    threads_per_block = threads_per_block < 1 ? 1 : threads_per_block;

                    printf("Blocks per thread %d\n", blocks_per_thread);
                    printf("Threads per block %d\n", threads_per_block);
                    fflush(stdout);

                    // int iter = max_threads < process_solution_spaces ? 1 : blocks_per_thread;
                    for (j = 0; j < blocks_per_thread; j++) {
                        BCB block = dequeue(&solution_queue);
                        
                        BCB new_block = {
                            .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
                            .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
                        };

                        memcpy(new_block.solution, block.solution, board.rows_count * board.cols_count * sizeof(CellState));
                        memcpy(new_block.solution_space_unknowns, block.solution_space_unknowns, board.rows_count * board.cols_count * sizeof(bool));

                        enqueue(&solution_queue, &block);
                        enqueue(&leaf_queues[i], &new_block);
                    }
                    
                    int solutions_to_skip = count / process_solution_spaces;
                    int threads_in_solution_space = threads_per_block;
                    if (size > SOLUTION_SPACES) {
                        threads_in_solution_space = starting_threads_in_solution_space;
                        solutions_to_skip += starting_solutions_to_skip;
                    }
                    printf("[%d] Starting task with %d %d %d %d\n", rank, i, threads_per_block, solutions_to_skip, threads_in_solution_space);
                    fflush(stdout);
                    total_processes_in_solution_spaces[i % process_solution_spaces] = threads_in_solution_space;
                    #pragma omp task firstprivate(i, threads_in_solution_space, solutions_to_skip)
                    {
                        task_find_solution_final(i, threads_in_solution_space, solutions_to_skip, blocks_per_thread);
                    }
                    count++;
                }
            } else {
                printf("[ERROR] Process %d has no solution spaces\n", rank);
                fflush(stdout);
            }
                // else {
                //     total_processes_in_solution_spaces = calloc(1, sizeof(int));
                //     while(!terminated) {
                //         if (rank == MANAGER_RANK) manager_check_messages();
                //         worker_check_messages(&solution_queue);
                //     }
                // }
        }
    }

        // if (received_block != NULL) {
        //     printf("Received block\n");
        //     // TODO: add received_block to solution_queue
        //     queue_size = 1;
        //     initializeQueue(&solution_queue, SOLUTION_SPACES);
        //     enqueue(&solution_queue, received_block);
        //     received_block = NULL;
        // } else {
        //     printf("Received block is NULL\n");
        // }
        // fflush(stdout);
        // break;
    // }

    return process_is_solver;
}

int main(int argc, char** argv) {

    /*
        Initialize MPI environment
    */

    // MPI_Init(&argc, &argv);
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr, "Insufficient thread support: required MPI_THREAD_FUNNELED, but got %d\n", provided);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if(rank == MANAGER_RANK) printf("Number of MPI processes: %d\n", size);
    if(rank == MANAGER_RANK) printf("Number of OMP threads: %d\n", omp_get_max_threads());

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    printf("Hello, world! I am process %d of %d on %s\n", rank, size, hostname);
    fflush(stdout);

    int max_threads = omp_get_max_threads();

    /*
        Type of placement according to the implementation

        Pure MPI (Distributed) -> scatter / scatter:excl (Spreads MPI processes across nodes for better bandwidth)
        Pure OpenMP -> pack:excl (Packs threads on one node and ensures exclusive access)
        Hybrid MPI + OpenMP -> scatter:excl	(Distributes MPI processes across nodes and gives them exclusive CPUs)
    */

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

    Board pruned = board;

    double pruning_start_time = MPI_Wtime();

    int i;
    if (rank == MANAGER_RANK) {

        Board (*techniques[])(Board) = {
            uniqueness_rule,
            sandwich_rules,
            pair_isolation,
            flanked_isolation,
            corner_cases
        };

        int num_techniques = sizeof(techniques) / sizeof(techniques[0]);
        
        int nt = omp_get_max_threads();
        nt = nt > SOLUTION_SPACES ? SOLUTION_SPACES : nt;
        
        Board *partials = malloc(num_techniques * sizeof(Board));

        #pragma omp parallel for num_threads(nt)
        for (i = 0; i < num_techniques; i++) 
        {
            if (DEBUG) printf("Technique %d\n", i);
            fflush(stdout);
            partials[i] = techniques[i](board);
        }

        for (i = 0; i < num_techniques; i++)
            pruned = combine_boards(pruned, partials[i], false, "Partial");

        if (DEBUG) printf("Finished initial pruning\n");
        fflush(stdout);
    
        /*
            Repeat the whiting and blacking pruning techniques until the solution doesn't change
        */

        while (true) {

            Board white_solution = set_white(pruned);
            Board black_solution = set_black(pruned);

            Board partial = combine_boards(pruned, white_solution, false, "Partial");
            Board new_solution = combine_boards(partial, black_solution, false, "Partial");

            if(!is_board_solution_equal(pruned, new_solution)) 
                pruned = new_solution;
            else 
                break;
        }
    }

    double pruning_end_time = MPI_Wtime();

    /*
        Broadcast the pruned solution to all the processes
    */

    MPI_Bcast(pruned.solution, board.rows_count * board.cols_count, MPI_INT, MANAGER_RANK, MPI_COMM_WORLD);
    memcpy(board.solution, pruned.solution, board.rows_count * board.cols_count * sizeof(CellState));

    // if (DEBUG) print_board("Pruned", pruned, SOLUTION);
    // if (DEBUG && rank == MANAGER_RANK) printf("[%d] Time for pruning: %f\n", rank, pruning_end_time - pruning_start_time);

    /*
        Initialize the backtracking variables
    */
    
    initializeQueue(&solution_queue, SOLUTION_SPACES);
    init_requests_and_messages();

    leaf_queues = malloc(max_threads * sizeof(Queue));
    for (i = 0; i < max_threads; i++) {
        initializeQueue(&leaf_queues[i], SOLUTION_SPACES);
    }

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
        Printing the pruned solution
    */

    if (rank == MANAGER_RANK) print_board("Pruned solution", pruned, SOLUTION);
    
    /*
        Print all the times
    */
    
    printf("[%d] Time for pruning part: %f\n", rank, pruning_end_time - pruning_start_time);
    MPI_Barrier(MPI_COMM_WORLD);
    
    printf("[%d] Time for recursive part: %f\n", rank, recursive_end_time - recursive_start_time);
    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == MANAGER_RANK) printf("\n\nTotal time: %f\n\n\n", rank, recursive_end_time - pruning_start_time);
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

    /*
        Free the memory and finalize the MPI environment
    */

    MPI_Type_free(&MPI_MESSAGE);

    // TODO: free all the memory before finalizing
   
    MPI_Finalize();

    return 0;
}