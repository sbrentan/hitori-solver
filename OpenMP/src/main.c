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

// ----- Backtracking variables -----
bool terminated = false;
// bool is_my_solution_spaces_ended = false;
int solutions_to_skip = 0;
// int total_processes_in_solution_space = 1;
int *unknown_index, *unknown_index_length;

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
                bool leaf_found = build_leaf(board, &blocks[i], 0, 0, &unknown_index, &unknown_index_length, &threads_in_solution_space, &solutions_to_skip);
                if (DEBUG) printf("[%d] Leaf built for solution space %d\n", rank, my_solution_spaces[i]);
                if (leaf_found) {
                    bool solution_found = bfs_white_cells_connected(board, &blocks[i], my_threads);
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
}


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
    Board pruned = techniques[0](board);
    print_board("------", pruned, SOLUTION);

    for (i = 1; i < num_techniques; i++) {
        Board partial = techniques[i](pruned);
        
        char *name = malloc(20 * sizeof(char));
        sprintf(name, "Partial %d", i);

        print_board(name, partial, SOLUTION);

        pruned = combine_boards(pruned, techniques[i](pruned), false, "Partial");
        print_board("------", pruned, SOLUTION);
    }

    print_board("Semi-Pruned", pruned, SOLUTION);
    
    /*
        Repeat the whiting and blacking pruning techniques until the solution doesn't change
    */

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
    
    printf("Time needed %f\n", pruning_end_time-pruning_start_time);
    print_board("Pruned", pruned, SOLUTION);

    return 0;

    /*
        Initialize the backtracking variables
    */
    
    memcpy(board.solution, pruned.solution, board.rows_count * board.cols_count * sizeof(CellState));
    initializeQueue(&solution_queue);

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

    print_board("Pruned solution", pruned, SOLUTION);
        
    printf("Time for pruning part: %f\n", pruning_end_time - pruning_start_time);
    
    printf("Time for recursive part: %f\n", recursive_end_time - recursive_start_time);
    
    if (solution_found) {
        write_solution(board);
        print_board("Solution found", board, SOLUTION);
    }

    return 0;
}