#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../include/utils.h"
#include "../include/board.h"

void write_solution(Board board) {

    /*
        Helper function to write the solution to the output file.
    */

    /*
        Parameters:
            - board: The board to write the solution.
    */

    FILE *fp = fopen("./output/output.txt", "w");

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (board.solution[i * board.cols_count + j] == WHITE) 
                fprintf(fp, "O ");
            else if (board.solution[i * board.cols_count + j] == BLACK) 
                fprintf(fp, "X ");
            else 
                fprintf(fp, "? ");
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
}

void print_vector(int *vector, int size) {

    /*
        Helper function to print a vector.
    */

    /*
        Parameters:
            - vector: The vector to print.
            - size: The size of the vector.
    */

    int i;
    for (i = 0; i < size; i++) {
        printf("%d ", vector[i]);
    }
    printf("\n");
}

void print_block(Board board, char *title, BCB* block) {
    
    /*
        Helper function to print the block.
    */

    /*
        Parameters:
            - board: The board.
            - title: The title of the block.
            - block: The block to print.
    */

    char buffer[MAX_BUFFER_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "# --- %s --- #\n", title);
    
    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (block->solution[i * board.cols_count + j] == WHITE) 
                strncat(buffer, "O ", sizeof(buffer) - strlen(buffer) - 1);
            else if (block->solution[i * board.cols_count + j] == BLACK) 
                strncat(buffer, "X ", sizeof(buffer) - strlen(buffer) - 1);
            else 
                strncat(buffer, "? ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    strncat(buffer, "\n --- Unknowns --- \n", sizeof(buffer) - strlen(buffer) - 1);

    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            char unknown[3];
            snprintf(unknown, sizeof(unknown), "%d ", block->solution_space_unknowns[i * board.cols_count + j]);
            strncat(buffer, unknown, sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    printf("%s", buffer);
    
}

void free_memory(int *pointers[]) {

    /*
        Helper function to free the memory allocated for all the pointers.
    */

    /*
        Parameters:
            - pointers: The array of pointers to free.
    */

    int i;
    int size = sizeof(pointers) / sizeof(pointers[0]);
    for (i = 0; i < size; i++) {
        free(pointers[i]);
    }
}
