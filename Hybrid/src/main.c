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
int rank, size;

// ----- Backtracking variables -----
bool terminated = false;
bool is_my_solution_spaces_ended = false;
int solutions_to_skip = 0;
int total_processes_in_solution_space = 1;
int *unknown_index, *unknown_index_length, *processes_in_my_solution_space;

// ----- Common variables -----
MPI_Datatype MPI_MESSAGE;

// ----- Worker variables -----
Message manager_message, receive_work_message, refresh_solution_space_message, finished_solution_space_message;
MPI_Request manager_request;   // Request for the workers to contact the manager
MPI_Request receive_work_request, refresh_solution_space_request, finished_solution_space_request; // dedicated worker-worker
int *receive_work_buffer, *send_work_buffer;

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

    solutions_to_skip = receive_work_message.data1;
    total_processes_in_solution_space = receive_work_message.data2;
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
    if (buffer_to_block(receive_work_buffer, &block_to_receive))
        enqueue(&solution_queue, &block_to_receive);

    // --- open refresh solution space message channel
    if (total_processes_in_solution_space > 1) {
        receive_message(&refresh_solution_space_message, source, &refresh_solution_space_request, W2W_MESSAGE * 4);
        if (!is_my_solution_spaces_ended)
            printf("[ERROR] Process %d received a refresh solution space message from process %d while not solution space ended\n", rank, source);
    }
    else 
        is_my_solution_spaces_ended = false;
}

void worker_send_work(int destination, int expected_queue_size) {
    
    int queue_size = getQueueSize(&solution_queue);
    bool invalid_request = terminated || is_my_solution_spaces_ended || queue_size == 0 || expected_queue_size != queue_size;
    if (invalid_request)
        printf("[ERROR] Process %d sending invalid send work request to process %d [%d, %d, %d, %d]\n", rank, destination, terminated, is_my_solution_spaces_ended, queue_size, expected_queue_size);

    BCB block_to_send;
    int solutions_to_skip_to_send = 0;
    int total_processes_in_solution_space_to_send = 0;

    if (!invalid_request) {
        if (queue_size == 1) {
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
        else if(queue_size > 1) {
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
    int flag = 1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        // printf("[%d] Testing\n", rank);
        MPI_Test(&manager_request, &flag, &status);
        // printf("[%d] Finished testing\n", rank);
        if (flag) {
            receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);

            if (status.MPI_SOURCE == -2)
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for manager message\n", rank);
            
            printf("[INFO] Process %d received a message from manager {%d}\n", rank, manager_message.type);
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
                if (processes_in_my_solution_space[manager_message.data1] == -1)
                    printf("[ERROR] Process %d received an invalid FINISHED_SOLUTION_SPACE message from process %d (already -1)\n", rank, status.MPI_SOURCE);
                processes_in_my_solution_space[manager_message.data1] = -1;
            }
            else {
                printf("[ERROR] Process %d received an invalid message type %d from manager\n", rank, manager_message.type);
                // return &manager_message;
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
                initializeQueue(&solution_queue);
                if (buffer_to_block(refresh_solution_space_buffer, &new_block))
                    enqueue(&solution_queue, &new_block);
            }
            else
                printf("[ERROR] Process %d received an invalid message type %d from process %d (instead of refresh)\n", rank, refresh_solution_space_message.type, status.MPI_SOURCE);
        }
    }
}

void manager_consume_message(Message *message, int source) {
    printf("[INFO] Process %d (manager) received a message from process %d {%d}\n", rank, source, message->type);
    int i; //sender_id;
    MPI_Request send_worker_request = MPI_REQUEST_NULL;
    if (message->type == TERMINATE) {
        for (i = 0; i < size; i++) {
            if (i == MANAGER_RANK || i == message->data1) continue;
            send_message(i, &send_worker_request, TERMINATE, source, -1, false, M2W_MESSAGE);
        }
        terminated = true;
    }
    else if (message->type == STATUS_UPDATE) {
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
    else
        printf("[ERROR] Process %d (manager) received an invalid message type %d from process %d\n", rank, message->type, source);
}

void manager_check_messages() {
    int flag = 1, sender_id = -1;
    MPI_Status status;
    while(flag) {
        flag = 0;
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
    int flag = 0;
    while(!flag && !terminated) {
        MPI_Test(request, &flag, MPI_STATUS_IGNORE);
        if (!flag && rank == MANAGER_RANK) manager_check_messages();
        if (!flag) worker_check_messages();
    }
}


/* ------------------ OMP ------------------ */
typedef enum SolutionSpaceStatus {
    NO_STATUS = 0,
    ASK_FOR_SHARING = 1,
    REGENERATED_SOLUTION_SPACE = 2
} SolutionSpaceStatus;
// TODO: convert to bool array

int threads_in_solution_spaces[SOLUTION_SPACES];
SolutionSpaceStatus solution_spaces_status[SOLUTION_SPACES];
double master_time = 0;
double master_wait_time = 0;
void find_solution_parallel() {
    int max_threads = omp_get_max_threads();
    int i, j;
    #pragma omp single
    {
        // double next_leaf_time_start, next_leaf_allocation_time_start;
        double start_time = omp_get_wtime();
        double wait_start;
        if (terminated) {
            if (DEBUG) printf("Solution found when building leaves\n");
        } else {
            if (DEBUG) printf("Solution queue: %d\n", getQueueSize(&solution_queue));
            fflush(stdout);

            int queue_size = getQueueSize(&solution_queue);
            for (i = 0; i < queue_size; i++) {

                int threads_in_solution_space = max_threads / queue_size;
                if (max_threads % queue_size > i)
                    threads_in_solution_space++;
                threads_in_solution_space = threads_in_solution_space < 1 ? 1 : threads_in_solution_space;

                threads_in_solution_spaces[i] = threads_in_solution_space;
                solution_spaces_status[i] = NO_STATUS;

                BCB block = dequeue(&solution_queue);
                for (j = 0; j < threads_in_solution_space; j++) {

                    #pragma omp task firstprivate(block, j, threads_in_solution_space, i)
                    {
                        BCB new_block = {
                            .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
                            .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
                        };
                        
                        int k, l;
                        for (k = 0; k < board.rows_count; k++) {
                            for (l = 0; l < board.cols_count; l++) {
                                new_block.solution[k * board.cols_count + l] = block.solution[k * board.cols_count + l];
                                new_block.solution_space_unknowns[k * board.cols_count + l] = block.solution_space_unknowns[k * board.cols_count + l];
                            }
                        }
                        
                        // task_find_solution_for_real(&new_block, j, threads_in_solution_space, i);
                    }
                    
                    printf("Task started\n");
                    fflush(stdout);
                }
            }
            #pragma omp taskwait
        }
        if (DEBUG) printf("Finished finding solution\n");
        fflush(stdout);

        master_time = omp_get_wtime() - start_time;

        wait_start = omp_get_wtime();
        #pragma omp taskwait
        master_wait_time += omp_get_wtime() - wait_start;
    }
}

void find_solution_serial() {
    int threads_in_solution_space = 1;
    int solutions_to_skip = 0;
    while(!terminated) {
        if (isEmpty(&solution_queue) || terminated) {
            if (DEBUG) printf("Solution queue is empty or terminated\n");
            fflush(stdout);
            break;
        }

        BCB current = dequeue(&solution_queue);

        bool leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);

        if (leaf_found) {
            
            bool solution_found = check_hitori_conditions(board, &current);
            
            if (solution_found) {
                terminated = true;
                memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
                if (DEBUG) printf("Solution found\n");
            } else {
                enqueue(&solution_queue, &current);
            }
        } else {
            if (DEBUG) printf("One solution space ended\n");
            fflush(stdout);
        }
    }
}

double task_time = 0;
double *individual_task_time;
void task_find_solution_for_real(BCB *block, int thread_id, int threads_in_solution_space, int solution_space_id) {

    int thread_num = omp_get_thread_num();
    
    printf("[%d] Address of struct: %p\n", thread_num, (void*)block);
    fflush(stdout);

    if (false) {
        #pragma omp critical
        {
            if (DEBUG) printf("[%d] Finding solution with %d %d %d\n", thread_num, thread_id, threads_in_solution_space, solution_space_id);
            fflush(stdout);
            printf("\n\nSolution\n");
            int i, j;
            for (i = 0; i < board.rows_count; i++) {
                for (j = 0; j < board.cols_count; j++) {
                    printf("%d ", block->solution[i * board.cols_count + j]);
                }
                printf("\n");
            }
            printf("\n\nUnknowns\n");
            fflush(stdout);

            for (i = 0; i < board.rows_count; i++) {
                for (j = 0; j < board.cols_count; j++) {
                    printf("%d ", block->solution_space_unknowns[i * board.cols_count + j]);
                }
                printf("\n");
            }
            printf("\n\n------------------\n\n");
            fflush(stdout);
        }
        
        sleep(1);
    }

    if(DEBUG) printf("[%d] Starting task\n", thread_num);
    fflush(stdout);

    bool skip_ask_for_sharing = false;
    while(!terminated) {
        double start_time = omp_get_wtime();

        // TODO: create local board (and unknowns?) to each thread ??
        if (true) {
            if (solution_spaces_status[solution_space_id] == ASK_FOR_SHARING) {
                bool regenerated = false;
                #pragma omp critical
                {
                    if (solution_spaces_status[solution_space_id] == ASK_FOR_SHARING){
                        solution_spaces_status[solution_space_id] = NO_STATUS;
                        threads_in_solution_space++;
                        threads_in_solution_spaces[solution_space_id] = threads_in_solution_space;
                        regenerated = true;
                    }
                }
                if (regenerated) {
                    
                    if (DEBUG) printf("Regenerating solution space\n");
                    fflush(stdout);

                    int i;
                    for (i = 1; i < threads_in_solution_space; i++) {
                        #pragma omp task firstprivate(solution_space_id, threads_in_solution_space, i, block)
                        task_find_solution_for_real(block, i, threads_in_solution_space, solution_space_id);
                    }
                    thread_id = 0;
                }
                continue;
            } else if (threads_in_solution_space != threads_in_solution_spaces[solution_space_id]) {
                #pragma omp critical
                {
                    if (threads_in_solution_space != threads_in_solution_spaces[solution_space_id])
                    skip_ask_for_sharing = true;
                }
                if (skip_ask_for_sharing) {
                    if (DEBUG) printf("Exiting because of threads in solution space\n");
                    fflush(stdout);
                    break;
                }
                continue;
            }
        }

        bool solution_found = false;// check_leaf(block, thread_id, threads_in_solution_space, solution_space_id);

        // TODO: get into other solution space

        #pragma omp atomic
        task_time += omp_get_wtime() - start_time;

        individual_task_time[thread_num] += omp_get_wtime() - start_time;

        if (solution_found){
            skip_ask_for_sharing = true;
            break;
        }

        #pragma omp taskyield
    }

    if (!skip_ask_for_sharing) {
        // TODO: get into other solution space
        #pragma omp critical
        solution_spaces_status[solution_space_id] = ASK_FOR_SHARING;

        if (DEBUG) printf("Asking for sharing\n");
        fflush(stdout);
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
            if (DEBUG) printf("[%d - %d] Solution found\n", rank, thread_num);
            fflush(stdout);
        } else {
            #pragma omp critical
            enqueue(&solution_queue, &block);
        }
    } else {
        if (DEBUG) printf("[%d - %d] Leaf not found\n", rank, thread_num);
    }
}

bool check_leaf(BCB *block, int thread_id, int threads_in_solution_space, int solution_space_id) {
    bool leaf_found = next_leaf(board, block, &unknown_index, &unknown_index_length, &threads_in_solution_space, &thread_id);

    if (!leaf_found) {
        if (DEBUG) printf("Leaf not found\n");
        fflush(stdout);
    } else {
        
        bool solution_found = check_hitori_conditions(board, block);
        
        if (solution_found) {
            #pragma omp critical
            {
                terminated = true;
                memcpy(board.solution, block->solution, board.rows_count * board.cols_count * sizeof(CellState));
            }
            if (DEBUG) printf("Solution found\n");
            fflush(stdout);
            return true;
        }
    }
    return false;
}

/* ------------------ MAIN ------------------ */

bool hitori_mpi_solution() {
    
    int max_threads = omp_get_max_threads();

    int i, count = 0;
    // int solution_space_manager[SOLUTION_SPACES];
    int my_solution_spaces[SOLUTION_SPACES];
    
    // memset(solution_space_manager, -1, SOLUTION_SPACES * sizeof(int));
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));
    
    for (i = 0; i < SOLUTION_SPACES; i++) {
        // solution_space_manager[i] = i % size;
        if (i % size == rank) {
            my_solution_spaces[count++] = i;
        }
        if (rank == MANAGER_RANK) {
            worker_statuses[i % size].queue_size++;
            worker_statuses[i % size].processes_sharing_solution_space = max_threads;
        }
    }

    if (DEBUG) printf("Processor %d has %d solution spaces\n", rank, count);

    
    #pragma omp parallel
    {
        // TODO: decide whether to change allocation of variables
        int i;
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
    
    int queue_size = getQueueSize(&solution_queue);
    if (queue_size > 0 && queue_size < count) {
        if (DEBUG) printf("Processor %d is sending status update\n", rank);
        MPI_Request status_update_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size, 1, false, W2M_MESSAGE);
    } else if (queue_size == 0) {
        if (DEBUG) printf("Processor %d is asking for work\n", rank);
        // TODO: restore
        // is_my_solution_spaces_ended = true;
        // MPI_Request ask_work_request = MPI_REQUEST_NULL;
        // send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
    }
    
    bool leaf_found = false;
    while(!terminated) {

        if (rank == MANAGER_RANK) manager_check_messages();
        worker_check_messages(&solution_queue);

        if (!terminated) {
            queue_size = getQueueSize(&solution_queue);
            if (queue_size > 0) {
                BCB current_solution = dequeue(&solution_queue);

                leaf_found = next_leaf(board, &current_solution, &unknown_index, &unknown_index_length, &total_processes_in_solution_space, &solutions_to_skip);

                if (leaf_found) {
                    if (check_hitori_conditions(board, &current_solution)) {
                        memcpy(board.solution, current_solution.solution, board.rows_count * board.cols_count * sizeof(CellState));
                        terminated = true;
                        MPI_Request terminate_message_request = MPI_REQUEST_NULL;
                        send_message(MANAGER_RANK, &terminate_message_request, TERMINATE, rank, -1, false, W2M_MESSAGE);
                        if (rank == MANAGER_RANK) manager_check_messages();
                        return true;
                    } else
                        enqueue(&solution_queue, &current_solution);
                } else {
                    // send update status to manager
                    if (queue_size > 1) {
                        MPI_Request status_update_request = MPI_REQUEST_NULL;
                        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size - 1, 1, false, W2M_MESSAGE);
                    } else if (queue_size == 1) {  // now 0
                        is_my_solution_spaces_ended = true;
                        printf("Processor %d is asking for work\n", rank);
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

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    printf("Hello, world! I am process %d of %d on %s\n", rank, size, hostname);
    fflush(stdout);

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

    if (rank == MANAGER_RANK) {

        Board (*techniques[])(Board) = {
            uniqueness_rule,
            sandwich_rules,
            pair_isolation,
            flanked_isolation,
            corner_cases
        };

        int i;
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

    if (DEBUG) print_board("Pruned", pruned, SOLUTION);
    if (DEBUG && rank == MANAGER_RANK) printf("[%d] Time for pruning: %f\n", rank, pruning_end_time - pruning_start_time);

    /*
        Initialize the backtracking variables
    */
    
    initializeQueue(&solution_queue);
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