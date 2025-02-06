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
Queue leaf_queue;

// ----- Backtracking variables -----
bool terminated = false;
// bool is_my_solution_spaces_ended = false;
int solutions_to_skip = 0;
// int total_processes_in_solution_space = 1;
int *unknown_index, *unknown_index_length;

// ----- Time variables -----
double backtracking_time = 0;
double check_time = 0;
double dfs_time = 0;
double conditions_time = 0;

double next_leaf_time = 0;
double next_leaf_allocation_time = 0;
double building_time = 0;
double task_time = 0;
double master_time = 0;
double master_wait_time = 0;
double thread_wait_time = 0;
double master_running_task = 0;



// // Define your node and solution space structures as needed
// typedef struct Node {
//     // your node fields...
// } Node;

// typedef struct SolutionSpace {
//     // For example, a pointer to a data structure holding nodes to explore.
//     // It could be a queue, stack, or any worklist.
//     // Here we just simulate a counter of remaining leaves.
//     int remaining;
//     // ... plus other fields needed to manage the exploration.
// } SolutionSpace;

// #define NUM_SPACES 4

// // Function to simulate getting the next leaf from a solution space.
// // Returns a pointer to a Node if available, or NULL if none.
// Node* get_next_leaf(SolutionSpace *ss) {
//     if (ss->remaining > 0) {
//         ss->remaining--;
//         // Allocate or otherwise obtain a leaf node.
//         Node* node = malloc(sizeof(Node));
//         // Initialize node...
//         return node;
//     }
//     return NULL;
// }

// // Function to process a node (compute some calculations)
// void process_node(Node *node) {
//     // Your computation goes here...
//     // For example, a dummy workload:
//     // ... do work ...
//     free(node); // clean up when done
// }

// bool hitori_openmp_solution() {
//     // Initialize solution spaces.
//     SolutionSpace spaces[NUM_SPACES];
//     for (int i = 0; i < NUM_SPACES; i++) {
//         spaces[i].remaining = 10; // for example, each has 10 leaves to process
//     }

//     // Set up OpenMP parallel region with a single thread to create tasks.
//     #pragma omp parallel
//     {
//         #pragma omp single nowait
//         {
//             // Continue until all solution spaces have no work left.
//             bool work_remaining = true;
//             while (work_remaining) {
//                 work_remaining = false;
//                 // For each solution space, create a task if there is work.
//                 for (int i = 0; i < NUM_SPACES; i++) {
//                     Node* next_leaf = get_next_leaf(&spaces[i]);
//                     if (next_leaf != NULL) {
//                         work_remaining = true;
//                         // Create a task to process this leaf.
//                         #pragma omp task firstprivate(i, next_leaf)
//                         {
//                             // Optionally, print which solution space is processed.
//                             printf("Processing leaf in solution space %d\n", i);
//                             process_node(next_leaf); 
//                             // Optionally, add new work to the solution space if needed.
//                         }
//                     }
//                 }
//                 // Wait until all tasks in this round are complete.
//                 #pragma omp taskwait
//             } // end while
//         } // end single
//     } // end parallel

//     return 0;
// }




void task_build_solution_space(int solution_space_id){
    printf("Building solution space %d\n", solution_space_id);
    BCB block = {
        .solution = malloc(board.rows_count * board.cols_count * sizeof(CellState)),
        .solution_space_unknowns = malloc(board.rows_count * board.cols_count * sizeof(bool))
    };
    init_solution_space(board, &block, solution_space_id, &unknown_index);
    int threads_in_solution_space = 1;
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
        if (!isEmpty(&leaf_queue)) {
            current = dequeue(&leaf_queue);
            current_found = true;
        }
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


bool task_openmp_solution() {
    int RESUME_THRESHOLD = 4;

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

        if (true) {

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
                            // if (queue_full) {
                            //     wait_start = omp_get_wtime();

                            //     #pragma omp taskwait

                            //     master_wait_time += omp_get_wtime() - wait_start;
                            //     queue_full = false;
                            // }

                            double running_start = omp_get_wtime();
                            #pragma omp task if(max_threads > 1 || !queue_full)
                            task_find_solution();
                            if (queue_full)
                                master_running_task += omp_get_wtime() - running_start;


                            // // Check if the leaf queue is full and wait until it is not full
                            // if (isFull(&leaf_queue)) {
                            //     if (DEBUG) printf("Leaf queue is full\n");
                            //     fflush(stdout);

                            //     // master_time += omp_get_wtime() - start_time;

                            //     wait_start = omp_get_wtime();
                            //     // #pragma omp taskwait

                                
                            //     while (true) {
                            //         if (terminated) break;
                            //         if (getQueueSize(&leaf_queue) <= RESUME_THRESHOLD) break;
                                    
                            //         #pragma omp taskyield  // Let other tasks run.
                                    
                            //         usleep(10000);  // Sleep for 10 millisecond to reduce CPU usage.
                            //     }

                            //     master_wait_time += omp_get_wtime() - wait_start;
                            //     if (DEBUG) printf("Leaf queue is not full\n");                            
                            //     fflush(stdout);
                            //     // break;
                            // }

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
    }

    return terminated;
}




/*
bool hitori_openmp_solution() {

    int max_threads = omp_get_max_threads();

    int i, count;

    int my_solution_spaces[SOLUTION_SPACES];
    memset(my_solution_spaces, -1, SOLUTION_SPACES * sizeof(int));

    int initial_threads = SOLUTION_SPACES > max_threads ? max_threads : SOLUTION_SPACES;
    if (DEBUG) printf("Initial threads: %d\n", initial_threads);
    #pragma omp parallel num_threads(initial_threads) private(i, my_solution_spaces, count)
    {
        count = 0;
        int rank = omp_get_thread_num();
        if (DEBUG) printf("Rank: %d\n", rank);
        for (i = rank; i < SOLUTION_SPACES; i += initial_threads) {
            if (DEBUG) printf("[%d] Adding solution space %d %d\n", rank, i, count);
            my_solution_spaces[count++] = i;
        }

        int my_threads = max_threads / initial_threads;
        int remaining_threads = max_threads % initial_threads;
        if (rank < remaining_threads) my_threads++;
        printf("[%d] My threads: %d\n", rank, my_threads);

        BCB blocks[SOLUTION_SPACES];
        if (DEBUG) printf("[%d] Before initialize solution spaces\n", rank);
        if (DEBUG) printf("[%d] Count: %d\n", rank, count);
        
        for (i = 0; i < count; i++) {
            if (!terminated) {
                if (DEBUG) printf("[%d] Initializing solution space %d with i %d\n", rank, my_solution_spaces[i], i);
                init_solution_space(board, &blocks[i], my_solution_spaces[i], &unknown_index);

                // TODO: Remove threads_in_solution_space and solutions_to_skip if not using them
                int threads_in_solution_space = 1;
                if (DEBUG) printf("[%d] Building leaf for solution space %d\n", rank, my_solution_spaces[i]);
                double backtracking_start_time = omp_get_wtime();
                bool leaf_found = build_leaf(board, &blocks[i], 0, 0, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
                backtracking_time += omp_get_wtime() - backtracking_start_time;
                if (DEBUG) printf("[%d] Leaf built for solution space %d\n", rank, my_solution_spaces[i]);
                if (leaf_found) {
                    // bool solution_found = bfs_white_cells_connected(board, &blocks[i], my_threads);

                    double check_start_time = omp_get_wtime();
                    bool solution_found = check_hitori_conditions(board, &blocks[i], &dfs_time, &conditions_time);
                    check_time += omp_get_wtime() - check_start_time;

                    if (solution_found) {
                        if (DEBUG) printf("[%d] Solution found on building\n", rank);
                        // TODO: do this pragmas block all threads or just the threads created by the last pragma?
                        #pragma omp critical
                        {
                            terminated = true;
                            memcpy(board.solution, blocks[i].solution, board.rows_count * board.cols_count * sizeof(CellState));
                        }
                    } else {
                        #pragma omp critical
                        {
                            enqueue(&solution_queue, &blocks[i]);
                        }
                    }

                } else {
                    if (DEBUG) printf("Failed to find leaf for solution space %d\n", my_solution_spaces[i]);
                }
            }
        }

        if (DEBUG) printf("[%d] Finished building leaves\n", rank);
        fflush(stdout);

        // terminated = true;
        bool current_found;


        while(!terminated) {
            if (!isEmpty(&solution_queue)) {
                BCB current;
                current_found = false;

                #pragma omp critical
                {
                    if (!isEmpty(&solution_queue)) {
                        current = dequeue(&solution_queue);
                        current_found = true;
                    }
                }

                // check if current is not empty
                if (!current_found) {
                    if (DEBUG) printf("[%d] Current is empty\n", rank);
                    continue;
                }

                // TODO: Remove threads_in_solution_space and solutions_to_skip if not using them
                int threads_in_solution_space = 1;
                bool leaf_found = next_leaf(board, &current, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
                if (DEBUG) printf("[%d] Next leaf found %d\n", rank, leaf_found);

                if (leaf_found) {
                    bool solution_found = bfs_white_cells_connected(board, &current, my_threads);
                    if (solution_found) {
                        #pragma omp critical
                        {
                            terminated = true;
                            memcpy(board.solution, current.solution, board.rows_count * board.cols_count * sizeof(CellState));
                        }
                        break;
                    } else {
                        if (DEBUG) printf("[%d] Enqueueing solution\n", rank);
                        #pragma omp critical
                        {
                            enqueue(&solution_queue, &current);
                        }
                    }
                } else {
                    if (DEBUG) printf("One solution space ended\n");
                }
            } else {

                // TODO: remove or change
                // break;
            }
        }
    }

    return terminated;
}*/

int main(int argc, char** argv) {
    
    omp_set_nested(1);  // TODO: remove?

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

    int i;
    int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

    double pruning_start_time = omp_get_wtime();

    Board pruned = board;
    #pragma omp parallel
    {
        #pragma omp single nowait
        {
            for (i = 0; i < num_techniques; i++) {
                #pragma omp task firstprivate(i)
                {
                    Board combined = combine_boards(pruned, techniques[i](board), false, "Partial");
                        
                    #pragma omp critical
                    {
                        pruned = combined;
                    }
                }
            }
        }
    }
    
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

    /*
        Initialize the backtracking variables
    */
    
    memcpy(board.solution, pruned.solution, board.rows_count * board.cols_count * sizeof(CellState));
    initializeQueue(&solution_queue, SOLUTION_SPACES);
    initializeQueue(&leaf_queue, LEAF_QUEUE_SIZE);

    /*
        Compute the unknown cells indexes
    */

    compute_unknowns(board, &unknown_index, &unknown_index_length);

    
    /*
        Apply the recursive backtracking algorithm to find the solution
    */

    double recursive_start_time = omp_get_wtime();
    bool solution_found = task_openmp_solution();
    double recursive_end_time = omp_get_wtime();

    print_board("Pruned solution", pruned, SOLUTION);
        
    printf("Time for pruning part: %f\n", pruning_end_time - pruning_start_time);
    
    printf("Time for recursive part: %f\n", recursive_end_time - recursive_start_time);

    // printf("Time for building: %f\n", building_time);
    printf("Time for tasks: %f\n", task_time);
    printf("Time for master: %f\n", master_time);
    printf("Time for master wait: %f\n", master_wait_time);
    printf("Time for thread wait: %f\n", thread_wait_time);
    printf("Time for master running task: %f\n", master_running_task);
    printf("Time for next leaf: %f\n", next_leaf_time);
    printf("Time for next leaf allocation: %f\n", next_leaf_allocation_time);
    
    if (solution_found) {
        write_solution(board);
        print_board("Solution found", board, SOLUTION);
    }

    return 0;
}