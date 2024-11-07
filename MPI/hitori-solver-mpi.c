#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_BUFFER_SIZE 2048
#define DEBUG true

int * board; // Original puzzle board
int rows_count = 0, cols_count = 0; // Number of rows and columns in the puzzle

// Helper function to read a matrix from the input file
void read_board() {
    FILE *fp = fopen("../test-cases/inputs/input-5x5.txt", "r");
    
    if (fp == NULL) {
        printf("Could not open file.\n");
        exit(1);
    }

    char line[MAX_BUFFER_SIZE];
    
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        rows_count++;

        if (rows_count == 1) {
            char *token = strtok(line, " ");
            do {
                cols_count++;
            } while (token = strtok(NULL, " "));
        }
    }

    board = malloc((rows_count * cols_count) * sizeof(int));

    rewind(fp);

    int rows = 0, cols;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        char *token = strtok(line, " ");

        cols = 0;
        do {  
            board[rows * cols_count + cols] = atoi(token);
            cols++;
        } while (token = strtok(NULL, " "));

        rows++;
    }

    fclose(fp);
} 

// Helper function to print a matrix
void print_board() {

    if(DEBUG) printf("\nRows: %d, Cols: %d\n", rows_count, cols_count);

    int i, j;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            printf("%d ", board[i * cols_count + j]);
        }
        printf("\n");
    }

    fflush(stdout);
}

int main() {
    
    read_board();

    print_board();

    return 0;
}