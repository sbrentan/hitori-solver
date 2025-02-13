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

/* ------------------ GLOBAL VARIABLES ------------------ */
Board board;
Queue solution_queue;
Queue *leaf_queues;

// ----- Backtracking variables -----
bool terminated = false;
// bool is_my_solution_spaces_ended = false;
// int solutions_to_skip = 0;
// int total_processes_in_solution_space = 1;
int *unknown_index, *unknown_index_length;

typedef enum SolutionSpaceStatus {
    NO_STATUS = 0,
    ASK_FOR_SHARING = 1,
    REGENERATED_SOLUTION_SPACE = 2
} SolutionSpaceStatus;
// TODO: convert to bool array

int threads_in_solution_spaces[SOLUTION_SPACES];
SolutionSpaceStatus solution_spaces_status[SOLUTION_SPACES];

// ----- Time variables -----
double backtracking_time = 0;
double check_time = 0;
double dfs_time = 0;
double conditions_time = 0;

// TODO: remove
double next_leaf_time = 0;
double next_leaf_allocation_time = 0;
double building_time = 0;
double task_time = 0;
double master_time = 0;
double master_wait_time = 0;
double thread_wait_time = 0;
double master_running_task = 0;

double *individual_task_time;


void task_build_solution_space(int solution_space_id){
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
        double dfs_time = 0;
        double conditions_time = 0;
        bool solution_found = check_hitori_conditions(board, &block, &dfs_time, &conditions_time);
        if (solution_found) {
            #pragma omp critical
            {
                terminated = true;
                memcpy(board.solution, block.solution, board.rows_count * board.cols_count * sizeof(CellState));
            }
            printf("Solution found\n");
            fflush(stdout);
        } else {
            #pragma omp critical
            enqueue(&solution_queue, &block);
        }
    }
}

void task_find_solution() {

    double start_time = omp_get_wtime();

    if (terminated) {
        if (DEBUG) printf("Exiting because of terminated\n");
        fflush(stdout);
        return;
    }

    if (DEBUG) printf("Finding solution\n");
    fflush(stdout);

    BCB current;
    bool current_found = false;

    double thread_start = omp_get_wtime();
    #pragma omp critical
    {
        thread_wait_time += omp_get_wtime() - thread_start;
        // if (!isEmpty(&leaf_queue)) {
        //     current = dequeue(&leaf_queue);
        //     current_found = true;
        // }
    }

    if (!current_found) {
        if (DEBUG) printf("Current is empty\n");
        fflush(stdout);
        return;
    }
    
    double dfs_time = 0;
    double conditions_time = 0;
    bool solution_found = check_hitori_conditions(board, &current, &dfs_time, &conditions_time);
    if (solution_found) {
        thread_start = omp_get_wtime();
        #pragma omp critical
        {
            thread_wait_time += omp_get_wtime() - thread_start;
            terminated = true;
            memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
        }
        if (DEBUG) printf("Solution found\n");
    }
    if (DEBUG) printf("Finished finding task\n");
    fflush(stdout);

    #pragma omp atomic
    task_time += omp_get_wtime() - start_time;
}

/*
bool task_openmp_solution() {
    // int RESUME_THRESHOLD = 4;

    int max_threads = omp_get_max_threads();

    if (DEBUG) printf("Solution spaces: %d\n", SOLUTION_SPACES);

    #pragma omp parallel
    {
        // TODO: decide whether to change allocation of variables
        int i;
        building_time = omp_get_wtime();
        #pragma omp single
        {
            for (i = 0; i < SOLUTION_SPACES; i++) {
                #pragma omp task firstprivate(i)
                task_build_solution_space(i);
            }
        }
        
        #pragma omp taskwait 

        building_time = omp_get_wtime() - building_time;

        if (DEBUG) printf("Finished building leaves\n");

        #pragma omp single
        {
            double next_leaf_time_start, next_leaf_allocation_time_start;
            double start_time = omp_get_wtime();
            double wait_start;
            if (terminated) {
                if (DEBUG) printf("Solution found when building leaves\n");
            } else {
                if (DEBUG) printf("Solution queue: %d\n", getQueueSize(&solution_queue));
                fflush(stdout);

                while(true) {

                    if (isEmpty(&solution_queue) || terminated) {
                        if (DEBUG) printf("Solution queue is empty or terminated\n");
                        fflush(stdout);
                        break;
                    }

                    BCB current = dequeue(&solution_queue);

                    int threads_in_solution_space = 1;
                    next_leaf_time_start = omp_get_wtime();
                    bool leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
                    next_leaf_time += omp_get_wtime() - next_leaf_time_start;

                    if (leaf_found) {
                        next_leaf_allocation_time_start = omp_get_wtime();
                        BCB new_leaf = {
                            .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
                            .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
                        };
                        memcpy(new_leaf.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
                        memcpy(new_leaf.solution_space_unknowns, current.solution_space_unknowns, board.rows_count * board.cols_count * sizeof(bool));
                        next_leaf_allocation_time += omp_get_wtime() - next_leaf_allocation_time_start;
                        
                        bool queue_full = isFull(&leaf_queue);
                        if (!queue_full) {
                            wait_start = omp_get_wtime();

                            #pragma omp critical
                            enqueue(&leaf_queue, &new_leaf);

                            master_wait_time += omp_get_wtime() - wait_start;
                        }


                        double running_start = omp_get_wtime();
                        #pragma omp task if(max_threads > 1 || !queue_full)
                        task_find_solution();
                        if (queue_full)
                            master_running_task += omp_get_wtime() - running_start;

                        enqueue(&solution_queue, &current);
                        if (DEBUG) printf("Enqueued solution\n");
                        fflush(stdout);
                    } else {
                        if (DEBUG) printf("One solution space ended\n");
                        fflush(stdout);
                    }
                }
            }
            if (DEBUG) printf("Finished finding solution\n");
            fflush(stdout);

            master_time = omp_get_wtime() - start_time;

            wait_start = omp_get_wtime();
            #pragma omp taskwait
            master_wait_time += omp_get_wtime() - wait_start;
        }
    }

    return terminated;
}
*/

bool check_leaf(BCB *block, int thread_id, int threads_in_solution_space, int solution_space_id) {
    bool leaf_found = next_leaf(board, block, &unknown_index, &unknown_index_length, &threads_in_solution_space, &thread_id);

    if (!leaf_found) {
        if (DEBUG) printf("Leaf not found\n");
        fflush(stdout);
    } else {
        
        bool solution_found = check_hitori_conditions(board, block, &dfs_time, &conditions_time);
        
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
        if (false) {
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

        bool solution_found = check_leaf(block, thread_id, threads_in_solution_space, solution_space_id);

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

void find_solution_parallel() {
    int max_threads = omp_get_max_threads();
    int i, j;
    double start_time = omp_get_wtime();
    double wait_start;
    #pragma omp single
    {
        // double next_leaf_time_start, next_leaf_allocation_time_start;
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
                    
                    #pragma omp task firstprivate(block, j, threads_in_solution_space, i, new_block)
                    {
                        task_find_solution_for_real(&new_block, j, threads_in_solution_space, i);
                    }
                    
                    printf("Task started\n");
                    fflush(stdout);
                }
            }
        }
    }
    
    if (DEBUG) printf("Finished finding solution\n");
    fflush(stdout);

    master_time = omp_get_wtime() - start_time;

    wait_start = omp_get_wtime();
    master_wait_time += omp_get_wtime() - wait_start;
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
            
            bool solution_found = check_hitori_conditions(board, &current, &dfs_time, &conditions_time);
            
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

void task_find_solution_final(int thread_id, int threads_in_solution_space, int solutions_to_skip, int blocks_per_thread) {
    Queue local_queue;
    initializeQueue(&local_queue, blocks_per_thread);

    // TODO: create local board
    printf("[%d] Creating local queue\n", thread_id);
    
    int i;
    #pragma omp critical
    {
        for(i = 0; i < blocks_per_thread; i++) {
            BCB block = dequeue(&leaf_queues[thread_id]);
            enqueue(&local_queue, &block);
        }
    }

    int queue_size = getQueueSize(&local_queue);
    printf("[%d] Local queue size: %d\n", thread_id, queue_size);
    fflush(stdout);
    if (queue_size > 1 && solutions_to_skip > 0) {
        printf("[%d] ERROR: More than one block in local queue\n", thread_id);
        exit(-1);
    }

    while(!terminated) {
        if (isEmpty(&local_queue) || terminated) {
            if (DEBUG) printf("[%d] Local queue is empty or terminated\n", thread_id);
            fflush(stdout);
            break;
        }

        BCB current = dequeue(&local_queue);
        
        bool leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
        if (leaf_found) {
            bool solution_found = check_hitori_conditions(board, &current, &dfs_time, &conditions_time);
            if (solution_found) {
                #pragma omp critical
                {
                    terminated = true;
                    memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
                }
                if (DEBUG) printf("[%d] Solution found\n", thread_id);
            } else {
                enqueue(&local_queue, &current);
            }
        } else {
            if (DEBUG) printf("[%d] One solution space ended\n", thread_id);
            fflush(stdout);
        }
    }

    // TODO: ask for sharing
}

bool task_openmp_solution_for_real() {
    // int RESUME_THRESHOLD = 4;

    int max_threads = omp_get_max_threads();

    individual_task_time = malloc(max_threads * sizeof(double));
    memset(individual_task_time, 0, max_threads * sizeof(double));

    if (DEBUG) printf("Solution spaces: %d\n", SOLUTION_SPACES);
    fflush(stdout);

    #pragma omp parallel
    {
        // TODO: decide whether to change allocation of variables
        int i, j;
        building_time = omp_get_wtime();
        #pragma omp single
        {
            for (i = 0; i < SOLUTION_SPACES; i++) {
                #pragma omp task firstprivate(i)
                task_build_solution_space(i);
            }
        }
        
        #pragma omp taskwait // TODO: remove?

        building_time = omp_get_wtime() - building_time;

        if (DEBUG) printf("Finished building leaves\n");
        fflush(stdout);

        #pragma omp single
        {
            
            int count = 0;
            for (i = 0; i < max_threads; i++) {
                int blocks_per_thread = SOLUTION_SPACES / max_threads;
                if (SOLUTION_SPACES % max_threads > i)
                    blocks_per_thread++;
                blocks_per_thread = blocks_per_thread < 1 ? 1 : blocks_per_thread;

                int threads_per_block = max_threads / SOLUTION_SPACES;
                if (max_threads % SOLUTION_SPACES > i)
                    threads_per_block++;
                threads_per_block = threads_per_block < 1 ? 1 : threads_per_block;

                printf("Blocks per thread %d\n", blocks_per_thread);
                printf("Threads per block %d\n", threads_per_block);
                fflush(stdout);

                // int iter = max_threads < SOLUTION_SPACES ? 1 : blocks_per_thread;
                for (j = 0; j < blocks_per_thread; j++) {
                    BCB block = dequeue(&solution_queue);
                    // deepcopy of the block  TODO
                    
                    BCB new_block = {
                        .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
                        .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
                    };

                    memcpy(new_block.solution, block.solution, board.rows_count * board.cols_count * sizeof(CellState));
                    memcpy(new_block.solution_space_unknowns, block.solution_space_unknowns, board.rows_count * board.cols_count * sizeof(bool));

                    enqueue(&solution_queue, &block);
                    enqueue(&leaf_queues[i], &new_block);
                }
                
                int solutions_to_skip = count / SOLUTION_SPACES;
                printf("Starting task with %d %d %d\n", i, threads_per_block, solutions_to_skip);
                fflush(stdout);
                #pragma omp task firstprivate(i, threads_per_block, solutions_to_skip)
                {
                    task_find_solution_final(i, threads_per_block, solutions_to_skip, blocks_per_thread);
                }
                count++;
            }
        }
    }

    return terminated;
}

int main(int argc, char** argv) {

    int max_threads = omp_get_max_threads();

    printf("Starting Hitori solver\n");
    printf("Max threads: %d\n", max_threads);
    fflush(stdout);

    /*
        Read the board from the input file
    */

    read_board(&board, argv[1]);
    
    /*
        Print the initial board
    */

    if (DEBUG) print_board("Initial", board, BOARD);

    /*
        Apply the basic hitori pruning techniques to the board.
    */

    Board (*techniques[])(Board) = {
        openmp_uniqueness_rule,
        openmp_sandwich_rules,
        openmp_pair_isolation,
        openmp_flanked_isolation,
        openmp_corner_cases
    };

    int i, j;
    int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

    double pruning_start_time = omp_get_wtime();

    Board pruned = {
        .rows_count = board.rows_count,
        .cols_count = board.cols_count,
        .grid = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
        .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState))
    };
    
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            pruned.grid[i * board.cols_count + j] = board.grid[i * board.cols_count + j];
            pruned.solution[i * board.cols_count + j] = board.solution[i * board.cols_count + j];
        }
    }
    
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

    double techniques_end_time = omp_get_wtime();
    while (true) {

        Board white_solution = openmp_set_white(pruned);
        Board black_solution = openmp_set_black(pruned);

        Board partial = combine_boards(pruned, white_solution, false, "Partial");
        Board new_solution = combine_boards(partial, black_solution, false, "Partial");

        if(!is_board_solution_equal(pruned, new_solution)) 
            pruned = new_solution;
        else 
            break;
    }
    double pruning_end_time = omp_get_wtime();
    
    printf("Time techniques needed %f\n", techniques_end_time-pruning_start_time);
    printf("Time setting needed %f\n", pruning_end_time-techniques_end_time);
    printf("Total Time pruning needed %f\n", pruning_end_time-pruning_start_time);
    print_board("Pruned", pruned, SOLUTION);
    fflush(stdout);

    /*
        Initialize the backtracking variables
    */
    
    memcpy(board.solution, pruned.solution, board.rows_count * board.cols_count * sizeof(CellState));
    initializeQueue(&solution_queue, SOLUTION_SPACES);
    // initializeQueueArray(&leaf_queues, max_threads, SOLUTION_SPACES); TODO: fix this ???

    leaf_queues = malloc(max_threads * sizeof(Queue));
    for (i = 0; i < max_threads; i++) {
        initializeQueue(&leaf_queues[i], SOLUTION_SPACES);
    }

    // print all addresses of the queues
    for (i = 0; i < max_threads; i++) {
        printf("Queue address %d: %p\n", i, (void*)&leaf_queues[i]);
    }

    /*
        Compute the unknown cells indexes
    */

    compute_unknowns(board, &unknown_index, &unknown_index_length);

    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    double recursive_start_time = omp_get_wtime();
    bool solution_found = task_openmp_solution_for_real();
    double recursive_end_time = omp_get_wtime();

    print_board("Pruned solution", pruned, SOLUTION);
        
    printf("Time for pruning part: %f\n", pruning_end_time - pruning_start_time);
    
    printf("Time for recursive part: %f\n", recursive_end_time - recursive_start_time);

    printf("Time for building: %f\n", building_time);
    printf("Time for tasks: %f\n", task_time);
    printf("Time for master: %f\n", master_time);
    printf("Time for master wait: %f\n", master_wait_time);
    printf("Time for thread wait: %f\n", thread_wait_time);
    printf("Time for master running task: %f\n", master_running_task);
    printf("Time for next leaf: %f\n", next_leaf_time);
    printf("Time for next leaf allocation: %f\n", next_leaf_allocation_time);
    // print individual task times
    for (i = 0; i < omp_get_max_threads(); i++) {
        printf("Time for task %d: %f\n", i, individual_task_time[i]);
    }
    
    if (solution_found) {
        write_solution(board);
        print_board("Solution found", board, SOLUTION);
    }
    fflush(stdout);

    return 0;
}