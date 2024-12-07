#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "libs/constants.h"
#include "libs/types.h"
#include "libs/queue.h"
#include "libs/utils.h"
#include "libs/board.h"
#include "pruning.h"
#include "backtracking.h"

int rank, size, solver_process = -1;
double ctime = 0;
Board board;
Queue solution_queue;
int *unknown_index, *unknown_index_length;
bool terminated = false;

int solutions_to_skip = 0;
int total_processes_in_solution_space = 1;
int *processes_in_my_solution_space;

bool is_my_solution_spaces_ended = false;


// TODO: as solution_queue is global, it is useless to pass it as argument to the functions


/* ------------------ MESSAGE PASSING VARIABLES ------------------ */

// ----- Common variables -----
MPI_Datatype MPI_MESSAGE;

// ----- Worker variables -----
Message manager_message, receive_work_message, refresh_solution_space_message, finished_solution_space_message;
MPI_Request manager_request;   // Request for the workers to contact the manager
MPI_Request receive_work_request, refresh_solution_space_request, finished_solution_space_request; // dedicated worker-worker
int *receive_work_buffer, *send_work_buffer;


// ----- Manager variables -----
Message *worker_messages;
MPI_Request *worker_requests;  // Requests for the manager to contact the workers
WorkerStatus *worker_statuses;


// when a process is manager of more solution spaces, and it concedes one of them. Who is the manager of the new transferred solution space?
// Solution space worker managers are no more
//  - At the beginning, as now, all now about the division of initial solution spaces (also manager)
//    If other processes ask for sharing it will redirect them to the right process (if it hasnt finished building leaf they will wait)


/* ------------------ FUNCTION DECLARATIONS ------------------ */

void wait_for_message(MPI_Request *request, Queue *queue);


/* ------------------ MESSAGE PASSING ------------------ */

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
        wait_for_message(request, &solution_queue);
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

void worker_receive_work(int source, Queue *queue) {
    int flag = 0;
    printf("[INFO] Process %d waiting work from process %d\n", rank, source);
    
    // --- receive initial message
    receive_message(&receive_work_message, source, &receive_work_request, W2W_MESSAGE);
    wait_for_message(&receive_work_request, queue);
    if (terminated) return;

    if (receive_work_message.invalid) {
        printf("[ERROR] Process %d received an invalid message from process while receiving work %d\n", rank, source);
        MPI_Request new_ask_for_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &new_ask_for_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
        return;
    }

    // TODO: check this 
    solutions_to_skip = receive_work_message.data1;
    total_processes_in_solution_space = receive_work_message.data2;
    printf("[INFO] Process %d received work from process %d with solutions to skip %d and total processes in solution space %d\n", rank, source, solutions_to_skip, total_processes_in_solution_space);

    // --- receive buffer
    MPI_Status status;
    MPI_Test(&receive_work_request, &flag, &status);
    if (!flag || status.MPI_SOURCE != -2)
        printf("[ERROR] MPI_Test in worker RECEIVE_WORK failed with flag %d and status %d\n", flag, status.MPI_SOURCE);
    MPI_Irecv(receive_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, source, W2W_BUFFER, MPI_COMM_WORLD, &receive_work_request);
    wait_for_message(&receive_work_request, queue);
    if (terminated) return;

    BCB block_to_receive;
    if (buffer_to_block(&board, receive_work_buffer, &block_to_receive))
        enqueue(queue, &block_to_receive);

    // --- open refresh solution space message channel
    if (total_processes_in_solution_space > 1) {
        receive_message(&refresh_solution_space_message, source, &refresh_solution_space_request, W2W_MESSAGE * 5);
        if (!is_my_solution_spaces_ended)
            printf("[ERROR] Process %d received a refresh solution space message from process %d while not solution space ended\n", rank, source);
    }
    else 
        is_my_solution_spaces_ended = false;
}

void worker_send_work(int destination, Queue *queue, int expected_queue_size) {
    
    int queue_size = getQueueSize(queue);
    bool invalid_request = terminated || is_my_solution_spaces_ended || queue_size == 0 || expected_queue_size != queue_size;
    if (invalid_request)
        printf("[ERROR] Process %d sending invalid send work request to process %d [%d, %d, %d, %d]\n", rank, destination, terminated, is_my_solution_spaces_ended, queue_size, expected_queue_size);

    BCB block_to_send;
    int solutions_to_skip_to_send = 0;
    int total_processes_in_solution_space_to_send = 0;

    if (!invalid_request) {
        if (queue_size == 1) {
            block_to_send = peek(queue);
            block_to_buffer(&board, &block_to_send, &send_work_buffer);
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
                send_message(i, &request, REFRESH_SOLUTION_SPACE, ++count, total_processes_in_solution_space, false, W2W_MESSAGE * 5);
            }
            // if (count != total_processes_in_solution_space_to_send - 2)
            //     printf("[ERROR] Process %d encountered wrong number of processes sharing solution space: %d %d\n", rank, count, total_processes_in_solution_space_to_send - 2);
            // else
            //     printf("[INFO] Process %d encountered correct number of processes sharing solution space: %d %d\n", rank, count, total_processes_in_solution_space_to_send - 2);
            
            // --- open channel for finished solution space message
            // Message finished_solution_space_message;
            // receive_message(&finished_solution_space_message, MPI_ANY_SOURCE, &finished_solution_space_request, W2W_MESSAGE);
        }
        else if(queue_size > 1) {
            block_to_send = dequeue(queue);
            block_to_buffer(&board, &block_to_send, &send_work_buffer);
            solutions_to_skip_to_send = 0;
            total_processes_in_solution_space_to_send = 1;
        }
    }
    printf("[%d] Sending initial message\n", rank);
    // --- send initial message
    MPI_Request send_work_request = MPI_REQUEST_NULL;
    send_message(destination, &send_work_request, WORKER_SEND_WORK, solutions_to_skip_to_send, total_processes_in_solution_space_to_send, invalid_request, W2W_MESSAGE);
    printf("[%d] Sent initial message\n", rank);

    // --- send buffer
    if (!invalid_request) {
        MPI_Request send_work_buffer_request;
        MPI_Isend(send_work_buffer, board.cols_count * board.rows_count * 2, MPI_INT, destination, W2W_BUFFER, MPI_COMM_WORLD, &send_work_buffer_request);
    }
}

void worker_check_messages(Queue *queue, bool skip_refresh_solution_space) {
    int flag = 1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        printf("[%d] Testing\n", rank);
        MPI_Test(&manager_request, &flag, &status);
        printf("[%d] Finished testing\n", rank);
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
                worker_send_work(manager_message.data1, queue, manager_message.data2);
            }
            else if (manager_message.type == RECEIVE_WORK) {
                worker_receive_work(manager_message.data1, queue);
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
            receive_message(&refresh_solution_space_message, status.MPI_SOURCE, &refresh_solution_space_request, W2W_MESSAGE * 5);
            if (skip_refresh_solution_space) {
                printf("[ERROR] Process %d received a refresh solution space message from process %d while it should not\n", rank, status.MPI_SOURCE);
                return;
            }
            if (refresh_solution_space_message.type == REFRESH_SOLUTION_SPACE) {
                // TODO: what if the process is inside a wait_for_message called from worker_receive_message
                int buffer_size = board.cols_count * board.rows_count * 2;
                int refresh_solution_space_buffer[buffer_size];
                if (status.MPI_SOURCE == -2)
                    printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for buffer solution refresh\n", rank);
                MPI_Irecv(refresh_solution_space_buffer, buffer_size, MPI_INT, status.MPI_SOURCE, W2W_BUFFER, MPI_COMM_WORLD, &refresh_solution_space_request);
                wait_for_message(&refresh_solution_space_request, queue);
                if (terminated) return;

                BCB new_block;
                initializeQueue(queue);
                if (buffer_to_block(&board, refresh_solution_space_buffer, &new_block))
                    enqueue(queue, &new_block);
            }
            else
                printf("[ERROR] Process %d received an invalid message type %d from process %d (instead of refresh)\n", rank, refresh_solution_space_message.type, status.MPI_SOURCE);
        }
    }
}

void manager_consume_message(Message *message, int source) {
    printf("[INFO] Process %d (manager) received a message from process %d {%d}\n", rank, source, message->type);
    int i; //sender_id;
    // sender_id = rank_to_request_mapping[source];
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

            // mapping between status.MPI_SOURCE and sender_id (corresponding to the index in the worker_requests array) [saved in rank_to_request_mapping]
            if (status.MPI_SOURCE != sender_id)
                printf("[ERROR] Process %d got error in mapping between status.MPI_SOURCE %d and sender_id %d, mapping: %d\n", rank, status.MPI_SOURCE, sender_id, status.MPI_SOURCE);

            receive_message(&worker_messages[sender_id], status.MPI_SOURCE, &worker_requests[sender_id], W2M_MESSAGE);
            manager_consume_message(&worker_messages[sender_id], status.MPI_SOURCE);
        }
    }
}

void wait_for_message(MPI_Request *request, Queue *queue) {
    int flag = 0;
    while(!flag && !terminated) {
        MPI_Test(request, &flag, MPI_STATUS_IGNORE);
        if (!flag && rank == MANAGER_RANK) manager_check_messages();
        if (!flag) worker_check_messages(queue, true);
    }
}

/* ------------------ MAIN ------------------ */

bool hitori_mpi_solution() {

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
            worker_statuses[i % size].processes_sharing_solution_space = 1;
        }
    }

    bool leaf_found = false;
    BCB blocks[SOLUTION_SPACES];

    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (my_solution_spaces[i] == -1) break;
        
        init_solution_space(&board, &blocks[i], my_solution_spaces[i], unknown_index);

        leaf_found = build_leaf(&board, &blocks[i], 0, 0, unknown_index, unknown_index_length, &solutions_to_skip, &total_processes_in_solution_space, rank);

        if (check_hitori_conditions(&board, &blocks[i])) {
            memcpy(board.solution, blocks[i].solution, board.rows_count * board.cols_count * sizeof(CellState));
            terminated = true;
            MPI_Request terminate_message_request = MPI_REQUEST_NULL;
            send_message(MANAGER_RANK, &terminate_message_request, TERMINATE, rank, -1, false, W2M_MESSAGE);
            if (rank == MANAGER_RANK) manager_check_messages();
            return true;
        }

        if (leaf_found) {
            enqueue(&solution_queue, &blocks[i]);
            count--;
        } else {
            printf("Processor %d failed to find a leaf\n", rank);
        }
    }
    // TODO: send status to manager if not all leafs found (CONVERT TO GATHER????)
    int queue_size = getQueueSize(&solution_queue);
    if (queue_size > 0 && count > 0) {
        MPI_Request status_update_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &status_update_request, STATUS_UPDATE, queue_size, 1, false, W2M_MESSAGE);
    } else if (queue_size == 0) {
        is_my_solution_spaces_ended = true;
        MPI_Request ask_work_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &ask_work_request, ASK_FOR_WORK, -1, -1, false, W2M_MESSAGE);
    }

    while(!terminated) {

        if (rank == MANAGER_RANK) manager_check_messages();
        worker_check_messages(&solution_queue, false);

        if (!terminated) {
            queue_size = getQueueSize(&solution_queue);
            if (queue_size > 0) {
                BCB current_solution = dequeue(&solution_queue);

                leaf_found = next_leaf(&board, &current_solution, unknown_index, unknown_index_length, &solutions_to_skip, &total_processes_in_solution_space, rank);

                if (leaf_found) {
                    if (check_hitori_conditions(&board, &current_solution)) {
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

    initializeQueue(&solution_queue);

    /*
        Read the board from the input file
    */

    if (rank == MANAGER_RANK) read_board(&board.grid, &board.rows_count, &board.cols_count, &board.solution);
    
    /*
        Share the board with all the processes by packing the data into a single array
    */
    
    mpi_share_board(&board, rank);

    init_requests_and_messages();

    MPI_Barrier(MPI_COMM_WORLD);
    printf("Initial board size %d %d\n", board.rows_count, board.cols_count);

    /*
        Apply the basic hitori pruning techniques to the board.
    */

    Board (*techniques[])(Board, int, int) = {
        mpi_uniqueness_rule,
        mpi_sandwich_rules,
        mpi_pair_isolation,
        mpi_flanked_isolation,
        mpi_corner_cases
    };

    int i;
    int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

    double pruning_start_time = MPI_Wtime();
    Board pruned_solution = techniques[0](board, rank, size);
    for (i = 1; i < num_techniques; i++) {
        printf("Technique %d\n", i);
        pruned_solution = combine_board_solutions(pruned_solution, techniques[i](pruned_solution, rank, size), false);
        if (DEBUG && rank == 0) print_board("Partial", pruned_solution, SOLUTION);
    }

    /*
        Repeat the whiting and blacking pruning techniques until the solution doesn't change
    */

    bool changed = true;
    while (changed) {

        Board white_solution = mpi_set_white(pruned_solution, rank, size);
        Board black_solution = mpi_set_black(pruned_solution, rank, size);
        Board partial = combine_board_solutions(pruned_solution, white_solution, false);
        Board new_solution = combine_board_solutions(partial, black_solution, false);

        if (DEBUG && rank == 0) print_board("Partial", new_solution, SOLUTION);
        if (rank == 0) changed = !is_board_equal(pruned_solution, new_solution, SOLUTION);
        
        MPI_Bcast(&changed, 1, MPI_C_BOOL, 0, MPI_COMM_WORLD);

        if (changed) pruned_solution = new_solution;
    }
    double pruning_end_time = MPI_Wtime();

    memcpy(board.solution, pruned_solution.solution, board.rows_count * board.cols_count * sizeof(CellState));
    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    Board final_solution = deep_copy(pruned_solution);
    double recursive_start_time = MPI_Wtime();
    compute_unknowns(&final_solution, &unknown_index, &unknown_index_length);

    bool solution_found = hitori_mpi_solution();
    if (solution_found) {
        printf("[%d] Solution found\n", rank);
        print_board("Solution", board, SOLUTION);
    }
    printf("Processor %d is finished\n", rank);
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

    if (solution_found) printf("Solution found by process %d\n", rank);
    if (solution_found) {
        write_solution(board);
        char formatted_string[MAX_BUFFER_SIZE];
        snprintf(formatted_string, MAX_BUFFER_SIZE, "\nSolution found by process %d", rank);
        print_board(formatted_string, board, SOLUTION);
    }

    /*
        Free the memory and finalize the MPI environment
    */

    // MPI_Type_free(&MPI_MESSAGE);

    // TODO: free all the memory before finalizing
   
    //free_memory((int *[]){board.grid, board.solution, pruned_solution.grid, pruned_solution.solution, final_solution.grid, final_solution.solution});
    printf("Finalizing...\n");
    MPI_Finalize();
    printf("Finalized\n");
    return 0;
}