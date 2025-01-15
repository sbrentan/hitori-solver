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
int rank, size;

int main(int argc, char** argv) {

    /*
        Read the board from the input file
    */
    size = strtol(argv[1], NULL, 10);
    printf("%d\n", size);
    return 0;

    read_board(&board, argv[2]);
    

    /*
        Print the initial board
    */

    if (DEBUG) print_board("Initial", board, BOARD);

    /*
        Apply the basic hitori pruning techniques to the board.
    */

    Board (*techniques[])(Board, int) = {
        openmp_uniqueness_rule,
        openmp_sandwich_rules,
        openmp_pair_isolation,
        openmp_flanked_isolation,
        openmp_corner_cases
    };

    int i;
    int num_techniques = sizeof(techniques) / sizeof(techniques[0]);

    double pruning_start_time = omp_get_wtime();
    Board pruned = techniques[0](board, size);
    for (i = 1; i < num_techniques; i++)
        pruned = combine_boards(pruned, techniques[i](pruned, size), false, "Partial");
    
    /*
        Repeat the whiting and blacking pruning techniques until the solution doesn't change
    */
   
    while (true) {

        Board white_solution = openmp_set_white(pruned, size);
        Board black_solution = openmp_set_black(pruned, size);

        Board partial = combine_boards(pruned, white_solution, false, "Partial");
        Board new_solution = combine_boards(partial, black_solution, false, "Partial");

        if(!is_board_solution_equal(pruned, new_solution)) 
            pruned = new_solution;
        else 
            break;
    }
    double pruning_end_time = omp_get_wtime();
}