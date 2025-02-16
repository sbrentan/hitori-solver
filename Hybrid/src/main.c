#include <mpi.h>
#include <omp.h>
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
Queue *leaf_queues;
int rank, size;

// ----- Backtracking variables -----
bool terminated = false;
bool process_is_solver = false;

int process_solution_spaces, starting_solutions_to_skip;
int *total_processes_in_solution_spaces;
int *unknown_index, *unknown_index_length;

// ----- Common variables -----
MPI_Datatype MPI_MESSAGE;

// ----- Worker variables -----
Message messagesqueue[MAX_MSG_SIZE];
int message_index = 0;

Message manager_message;
MPI_Request manager_request;   // Request for the workers to contact the manager

bool send_status_update_message = false;
bool send_terminate_message = false;

// ----- Manager variables -----
Message *worker_messages;
MPI_Request *worker_requests;   // Requests for the manager to contact the workers


/* ------------------ FUNCTION DECLARATIONS ------------------ */

void receive_message(Message *message, int source, MPI_Request *request, int tag) {
    if (size == 1) return;
    
    if (omp_get_thread_num() != MANAGER_THREAD) {
        printf("[ERROR] Process %d got invalid thread number %d while receiving\n", rank, omp_get_thread_num());
        return;
    }

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
    if (size == 1) return;
    if (omp_get_thread_num() != MANAGER_THREAD) {
        printf("[ERROR] Process %d got invalid thread number %d while sending\n", rank, omp_get_thread_num());
        return;
    }
    
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
        int i;
        for (i = 0; i < size; i++) {
            worker_requests[i] = MPI_REQUEST_NULL;
            receive_message(&worker_messages[i], i, &worker_requests[i], W2M_MESSAGE);
        }
    }
    manager_request = MPI_REQUEST_NULL;
    receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);
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
        #pragma omp atomic write
        terminated = true;
    }

    int flag = 1;
    MPI_Status status;
    while(flag) {
        flag = 0;
        // Test if the worker has received a message from the manager
        MPI_Test(&manager_request, &flag, &status);
        if (flag) {
            // Open a new Manager-to-Worker channel
            receive_message(&manager_message, MANAGER_RANK, &manager_request, M2W_MESSAGE);

            if (status.MPI_SOURCE == -2)
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE while waiting for manager message\n", rank);
            
            if (DEBUG) printf("[INFO] Process %d received a message from manager {%d}\n", rank, manager_message.type);

            /*
                Based on the received message, the worker will take the appropriate action.
            */

            if (manager_message.type == TERMINATE) {
                terminated = true;
                return;
            }
            else if (DEBUG)
                printf("[ERROR] Process %d received an invalid manager_message type %d from manager\n", rank, manager_message.type);
        }
    }
}

void manager_consume_message(Message *message, int source) {

    /*
        Based on the message type, the manager will take the appropriate action.

        TERMINATE: The manager will send a TERMINATE message to all the workers
    */

    if (size == 1) return;

    if (DEBUG) printf("[INFO] Process %d (manager) received a message from process %d {%d}\n", rank, source, message->type);
    
    int i; //sender_id;
    MPI_Request send_worker_request = MPI_REQUEST_NULL;
    if (message->type == TERMINATE) {
        // Send termination message to all the workers, except for itself
        for (i = 0; i < size; i++) {
            if (i == MANAGER_RANK) continue;
            send_message(i, &send_worker_request, TERMINATE, source, -1, false, M2W_MESSAGE);
        }
        terminated = true;
    } else if (DEBUG) 
        printf("[ERROR] Process %d (manager) received an invalid message type %d from process %d\n", rank, message->type, source);
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
                // should not happen
                printf("[ERROR] Process %d got -2 in status.MPI_SOURCE (SHOULD NOT HAPPEN)\n", rank);
                continue;
            }

            if (status.MPI_SOURCE != sender_id) {
                printf("[ERROR] Process %d got error in mapping between status.MPI_SOURCE %d and sender_id %d, mapping: %d\n", rank, status.MPI_SOURCE, sender_id, status.MPI_SOURCE);
                continue;
            }

            // open a new message channel
            receive_message(&worker_messages[sender_id], status.MPI_SOURCE, &worker_requests[sender_id], W2M_MESSAGE);
            
            // consume the actual message
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
    
    if (DEBUG) {
        printf("Building solution space %d\n", solution_space_id);
        fflush(stdout);
    }
    
    // Initialize the starting block
    BCB block = {
        .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
        .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
    };

    // Fill the block 
    init_solution_space(board, &block, solution_space_id, &unknown_index);
    
    int solutions_to_skip = 0, threads_in_solution_space = 1;
    // Find the first leaf
    bool leaf_found = build_leaf(board, &block, 0, 0, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
    
    if (leaf_found) {
        // If a leaf is found, check if it is a solution
        if (check_hitori_conditions(board, &block)) {
            // if it is a solution, copy it to the global solution and set termination flags
            #pragma omp critical
            {
                terminated = true;
                memcpy(board.solution, block.solution, board.rows_count * board.cols_count * sizeof(CellState));
            }
            if (DEBUG) {
                printf("Solution found\n");
                fflush(stdout);
            }
        } else {
            // if it is not a solution, enqueue it to the global solution queue
            #pragma omp critical
            enqueue(&solution_queue, &block);
        }
    } else {
        if (DEBUG) {
            printf("[%d - %d] Leaf not found\n", rank, thread_num);
            fflush(stdout);
        }
    }
}

void task_find_solution(int thread_id, int threads_in_solution_space, int solutions_to_skip, int blocks_per_thread) {
    
    /*
        Instantiate a local queue for each thread
    */
    
    Queue local_queue;
    initializeQueue(&local_queue, blocks_per_thread);
    
    int i;

    /*
        Open a critical region to carefully copy the blocks assigned from the master to the local queue
    */

    #pragma omp critical
    {
        for(i = 0; i < blocks_per_thread; i++) {
            BCB block = dequeue(&leaf_queues[thread_id]);
            enqueue(&local_queue, &block);
        }
    }

    int queue_size = getQueueSize(&local_queue);
    
    if (DEBUG) {
        printf("[%d][%d] Local queue size: %d %f\n",rank, thread_id, queue_size, omp_get_wtime());
        fflush(stdout);
    }
    
    if (queue_size > 1 && solutions_to_skip > 0) {
        printf("[%d] ERROR: More than one block in local queue\n", thread_id);
        fflush(stdout);
        exit(-1);
    }

    bool leaf_found = false;
    while(!terminated) {
        
        // Only the manager thread will check the messages
        if (omp_get_thread_num() == MANAGER_THREAD) {
            worker_check_messages();
            if (rank == MANAGER_RANK) manager_check_messages();
        }

        queue_size = getQueueSize(&local_queue);
        
        if (terminated) {
            if (DEBUG) {
                printf("[%d][%d] Terminated\n", rank, thread_id);
                fflush(stdout);
            }
            break;
        }

        if (queue_size > 0) {
            
            // Dequeue the block from the local queue
            BCB current = dequeue(&local_queue);
            leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
            
            if (leaf_found) {
                if (check_hitori_conditions(board, &current)) {
                    // a solution has been found, so the flags for termination are set
                    #pragma omp critical
                    {
                        send_terminate_message = true;
                        process_is_solver = true;
                        memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
                    }

                    if (DEBUG) {
                        printf("[%d] [%d] Solution found\n", rank, thread_id);
                        fflush(stdout);
                    }

                    if (omp_get_thread_num() != MANAGER_THREAD) break;

                } else {
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
                
                if (DEBUG) {
                    printf("[%d] One solution space ended\n", thread_id);
                    fflush(stdout);
                }
            }
        } else {
            if (omp_get_thread_num() != MANAGER_THREAD) break;
        }
    }
    
    if (DEBUG) {
        printf("[%d][%d] Exiting\n",rank , thread_id);
        fflush(stdout);
    }
}

/* ------------------ MAIN ------------------ */

bool hitori_hybrid_solution() {
    
    int max_threads = omp_get_max_threads();

    int i, j, count = 0;
    int my_solution_spaces[SOLUTION_SPACES];
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));
    
    int starting_threads_in_solution_space = max_threads;
    for (i = 0; i < SOLUTION_SPACES; i++) {
        if (i % size == rank) {
            my_solution_spaces[count++] = i;
            if (size > SOLUTION_SPACES && i + SOLUTION_SPACES < size)
                starting_threads_in_solution_space += max_threads;
        }
    }

    starting_solutions_to_skip = 0;

    if (size > SOLUTION_SPACES * 2) {
        // for keeping the algorithm simpler, this constraint was introduced
        printf("[ERROR] Max processes is greater than 2 * SOLUTION_SPACES\n");
        return false;
    }

    for (i = SOLUTION_SPACES; i < size; i++) {
        if (i % size == rank) {
            my_solution_spaces[count++] = i % SOLUTION_SPACES;
            starting_solutions_to_skip = max_threads;
            starting_threads_in_solution_space += max_threads;
        }
    }

    if (DEBUG) {
        printf("Processor %d has %d solution spaces with %d starting solutions to skip and %d starting threads in solution space\n", rank, count, starting_solutions_to_skip, starting_threads_in_solution_space);
        fflush(stdout);
    }
    
    #pragma omp parallel
    {
        // Random pick one thread as the master that will spawn the tasks
        #pragma omp single
        {
            for (i = 0; i < count; i++) {
                #pragma omp task firstprivate(i, my_solution_spaces)
                task_build_solution_space(my_solution_spaces[i]);
            }
        }
    }

    // Implicitly wait for all tasks to finish
    
    if (DEBUG) printf("Processor %d finished finding solution: %d\n", rank, getQueueSize(&solution_queue));
    
    if (terminated) {
        if (DEBUG) printf("Processor %d is about to terminate\n", rank);
        
        // Send terminate message to the master process
        MPI_Request terminate_message_request = MPI_REQUEST_NULL;
        send_message(MANAGER_RANK, &terminate_message_request, TERMINATE, rank, -1, false, W2M_MESSAGE);

        // if master process, also wait for all workers to terminate
        if (rank == MANAGER_RANK) manager_check_messages();
        return true;
    }

    /*
        Send the initial statuses to the manager. If the queue is not empty, send a status update message.
        Otherwise, ask for work.
    */
    
    int queue_size = getQueueSize(&solution_queue);
    
    #pragma omp parallel
    {
        #pragma omp single
        {
            process_solution_spaces = queue_size;
            total_processes_in_solution_spaces = calloc(process_solution_spaces, sizeof(int));

            if (DEBUG && process_solution_spaces > 1 && starting_solutions_to_skip != 0) {
                // should not happen
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

                    if (DEBUG) {
                        printf("Blocks per thread %d\n", blocks_per_thread);
                        printf("Threads per block %d\n", threads_per_block);
                        fflush(stdout);
                    }

                    // Distribute the blocks to the threads
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
                    
                    if (DEBUG) {
                        printf("[%d] Starting task with %d %d %d %d\n", rank, i, threads_per_block, solutions_to_skip, threads_in_solution_space);
                        fflush(stdout);
                    }
                    
                    total_processes_in_solution_spaces[i % process_solution_spaces] = threads_in_solution_space;
                    
                    #pragma omp task firstprivate(i, threads_in_solution_space, solutions_to_skip)
                    task_find_solution(i, threads_in_solution_space, solutions_to_skip, blocks_per_thread);

                    count++;
                }
            } else if (DEBUG) {
                printf("[ERROR] Process %d has no solution spaces\n", rank);
                fflush(stdout);
            }
        }
        
        if (rank == MANAGER_RANK && omp_get_thread_num() == MANAGER_THREAD) {
            while(!terminated){
                worker_check_messages();
                manager_check_messages();
            }
        }
    }

    return process_is_solver;
}

int main(int argc, char** argv) {

    /*
        Initialize MPI environment
    */

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr, "Insufficient thread support: required MPI_THREAD_FUNNELED, but got %d\n", provided);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

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

    int max_threads = omp_get_max_threads();

    double pruning_start_time = MPI_Wtime();
    if (rank == MANAGER_RANK) {

        Board (*techniques[])(Board) = {
            uniqueness_rule,
            sandwich_rules,
            pair_isolation,
            flanked_isolation,
            corner_cases
        };
        int num_techniques = sizeof(techniques) / sizeof(techniques[0]);
    
        Board *partials = malloc(num_techniques * sizeof(Board));
        
        int max_threads = omp_get_max_threads();
        int threads_for_techniques = max_threads > num_techniques ? num_techniques : max_threads;
        
        int i;
        #pragma omp parallel num_threads(threads_for_techniques)
        {
            #pragma omp single
            {
                for (i = 0; i < num_techniques; i++) {
                    #pragma omp task firstprivate(i)
                    partials[i] = techniques[i](board);
                }
            }
        }
    
        // Implicitly wait for all the tasks to finish
    
        for (i = 0; i < num_techniques; i++)
            board = combine_boards(board, partials[i], false, "Partial");
        
        /*
            Repeat the whiting and blacking pruning techniques until the solution doesn't change
        */
    
        while (true) {
    
            Board white_solution = set_white(board);
            Board black_solution = set_black(board);
    
            Board partial = combine_boards(board, white_solution, false, "Partial");
            Board new_solution = combine_boards(partial, black_solution, false, "Partial");
    
            if(!is_board_solution_equal(board, new_solution)) 
                board = new_solution;
            else 
                break;
        }
    }
    double pruning_end_time = MPI_Wtime();

    if (DEBUG && rank == MANAGER_RANK) print_board("Pruned", board, SOLUTION);

    /*
        Share the pruned board with all the processes
    */

    mpi_share_board(&board, rank);

    /*
        Initialize the backtracking variables
    */
    
    initializeQueue(&solution_queue, SOLUTION_SPACES);
    initializeQueueArray(&leaf_queues, max_threads, SOLUTION_SPACES);
    init_requests_and_messages();

    /*
        Compute the unknown cells indexes
    */

    compute_unknowns(board, &unknown_index, &unknown_index_length);
    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    double recursive_start_time = MPI_Wtime();
    bool solution_found = hitori_hybrid_solution();
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

    /*
        Free the memory and finalize the MPI environment
    */

    free_memory((int *[]){
        unknown_index, 
        unknown_index_length
    });
    
    free(worker_messages);
    free(worker_requests);

    MPI_Type_free(&MPI_MESSAGE);
    MPI_Finalize();

    return 0;
}