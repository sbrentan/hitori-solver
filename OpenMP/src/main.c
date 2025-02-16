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
int *unknown_index, *unknown_index_length;

void task_build_solution_space(int solution_space_id){
    
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
            // if it is a solution, copy it to the global solution and terminate
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
        printf("[%d] Local queue size: %d\n", thread_id, queue_size);
        fflush(stdout);
    }
    
    if (queue_size > 1 && solutions_to_skip > 0) {
        printf("[%d] ERROR: More than one block in local queue\n", thread_id);
        fflush(stdout);
        exit(-1);
    }

    /*
        Start the backtracking algorithm
    */

    bool leaf_found = false;
    while(!terminated) {
        if (isEmpty(&local_queue) || terminated) {
            if (DEBUG) {
                printf("[%d] Local queue is empty or terminated\n", thread_id);
                fflush(stdout);
            }
            break;
        }

        // Dequeue the block from the local queue
        BCB current = dequeue(&local_queue);
        leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
        
        // If a leaf is found, check if it is a solution
        if (leaf_found) {
            if (check_hitori_conditions(board, &current)) {
                // if it is a solution, copy it to the global solution and terminate
                #pragma omp critical
                {
                    terminated = true;
                    memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
                }
                if (DEBUG) {
                    printf("[%d] Solution found\n", thread_id);
                    fflush(stdout);
                }
            } else {
                // if it is not a solution, enqueue it to the local queue and try again with the next solution
                enqueue(&local_queue, &current);
            }
        } else if (DEBUG) {
            printf("[%d] One solution space ended\n", thread_id);
            fflush(stdout);
        }
    }
}

bool hitori_openmp_solution() {

    int max_threads = omp_get_max_threads();

    if (DEBUG) {
        printf("Solution spaces: %d\n", SOLUTION_SPACES);
        fflush(stdout);
    }

    /*
        Open the parallel, in which all the threads will spawn
    */
    
    #pragma omp parallel
    {
        int i, j;
        // Random pick one thread as the master that will spawn the tasks
        #pragma omp single
        {
            for (i = 0; i < SOLUTION_SPACES; i++) {
                #pragma omp task firstprivate(i) // Each task will have its own copy of i
                task_build_solution_space(i); 
            }
        }

        // Implicitly wait for all the tasks to finish

        if (DEBUG) {
            printf("Finished building leaves\n");
            fflush(stdout);
        }

        // Random pick one thread as the master that will spawn the tasks
        #pragma omp single
        {
            int count = 0;
            for (i = 0; i < max_threads; i++) {

                /*
                    Determine the number of blocks per thread and the number of threads per block
                */

                int blocks_per_thread = SOLUTION_SPACES / max_threads;
                if (SOLUTION_SPACES % max_threads > i)
                    blocks_per_thread++;
                blocks_per_thread = blocks_per_thread < 1 ? 1 : blocks_per_thread;

                int threads_per_block = max_threads / SOLUTION_SPACES;
                if (max_threads % SOLUTION_SPACES > i)
                    threads_per_block++;
                threads_per_block = threads_per_block < 1 ? 1 : threads_per_block;

                if (DEBUG) {
                    printf("Blocks per thread %d\n", blocks_per_thread);
                    printf("Threads per block %d\n", threads_per_block);
                    fflush(stdout);
                }

                // Distribute the blocks to the threads
                for (j = 0; j < blocks_per_thread; j++) {
                    BCB new_block, block = dequeue(&solution_queue);
                    memcpy(&new_block, &block, sizeof(BCB));

                    enqueue(&solution_queue, &block);
                    enqueue(&leaf_queues[i], &new_block);
                }
                
                int solutions_to_skip = count / SOLUTION_SPACES;
                
                if (DEBUG) {
                    printf("Starting task with %d %d %d\n", i, threads_per_block, solutions_to_skip);
                    fflush(stdout);
                }
                
                // Spawn the tasks to find the solution, each having its own skip value
                #pragma omp task firstprivate(i, threads_per_block, solutions_to_skip)
                task_find_solution(i, threads_per_block, solutions_to_skip, blocks_per_thread);
                
                count++;
            }
        }
    }

    // Implicitly wait for all the tasks to finish

    return terminated;
}

int main(int argc, char** argv) {

    /*
        Read the board from the input file
    */

    if (argc != 2) {
        printf("[ERROR] input file not provided!\n");
        exit(-1);
    }

    read_board(&board, argv[1]);
    
    /*
        Print the initial board
    */

    if (DEBUG) {
        print_board("Initial", board, BOARD);
        fflush(stdout);
    }

    /*
        Apply the basic hitori pruning techniques to the board.
    */

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
    double pruning_start_time = omp_get_wtime();
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
    double pruning_end_time = omp_get_wtime();

    if (DEBUG) {
        print_board("Pruned", board, SOLUTION);
        fflush(stdout);
    }

    /*
        Initialize the backtracking variables
    */
    
    initializeQueue(&solution_queue, SOLUTION_SPACES);
    initializeQueueArray(&leaf_queues, max_threads, SOLUTION_SPACES);

    /*
        Compute the unknown cells indexes
    */

    compute_unknowns(board, &unknown_index, &unknown_index_length);
    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    double recursive_start_time = omp_get_wtime();
    bool solution_found = hitori_openmp_solution();
    double recursive_end_time = omp_get_wtime();

    /*
        Print all the times
    */
        
    printf("Time for pruning part: %f\n", pruning_end_time - pruning_start_time);
    
    printf("Time for recursive part: %f\n", recursive_end_time - recursive_start_time);

    printf("Total execution time: %f\n", recursive_end_time - pruning_start_time);

    fflush(stdout);

    /*
        Write the final solution to the output file
    */
    
    if (solution_found) {
        write_solution(board);
        print_board("Solution found", board, SOLUTION);
        fflush(stdout);
    }

    return 0;
}