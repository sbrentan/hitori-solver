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

/* ------------------ GLOBAL VARIABLES ------------------ */
Board board;
Queue solution_queue;

// ----- Backtracking variables -----
// bool terminated = false;
// bool is_my_solution_spaces_ended = false;
// int solutions_to_skip = 0;
// int total_processes_in_solution_space = 1;
int *unknown_index, *unknown_index_length;

bool hitori_openmp_solution() {

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
        
        init_solution_space(board, &blocks[i], my_solution_spaces[i], &unknown_index);

        leaf_found = build_leaf(board, &blocks[i], 0, 0, &unknown_index, &unknown_index_length, &total_processes_in_solution_space, &solutions_to_skip);

        if (check_hitori_conditions(board, &blocks[i])) {
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
    for (i = 1; i < num_techniques; i++)
        pruned = combine_boards(pruned, techniques[i](pruned), false, "Partial");
    
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

    /*
        Initialize the backtracking variables
    */
    
    board = pruned;
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
    
    printf(" Time for recursive part: %f\n", recursive_end_time - recursive_start_time);
    
    if (solution_found) {
        write_solution(board);
        print_board("Solution found", board, SOLUTION);
    }
}