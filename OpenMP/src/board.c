#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/board.h"

void read_board(Board* board, char *filename) {

    /*
        Helper function to read the board from the input file.
    */

    // Concat INPUT_PATH with the filename
    char path[MAX_BUFFER_SIZE];
    snprintf(path, sizeof(path), "%s%s", INPUT_PATH, filename);
    
    FILE *fp = fopen(path, "r");
    
    if (fp == NULL) {
        printf("Could not open file.\n");
        exit(1);
    }

    char line[MAX_BUFFER_SIZE];
    
    int rows = 0, cols = 0;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        rows++;

        if (rows == 1) {
            char *token = strtok(line, " ");
            do {
                cols++;
            } while ((token = strtok(NULL, " ")));
        }
    }

    board->rows_count = rows;
    board->cols_count = cols;

    if (DEBUG) {
        printf("Rows: %d\n", rows);
        printf("Cols: %d\n", cols);
    }

    if (rows != cols) {
        printf("The board must be a square.\n");
        exit(1);
    }

    board->grid = (int *) malloc(rows * cols * sizeof(int));
    board->solution = (int *) malloc(rows * cols * sizeof(CellState));

    rewind(fp);

    int temp_row = 0, temp_col;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        char *token = strtok(line, " ");

        temp_col = 0;
        do {  
            board->grid[temp_row * cols + temp_col] = atoi(token);
            board->solution[temp_row * cols + temp_col] = UNKNOWN;
            temp_col++;
        } while ((token = strtok(NULL, " ")));

        temp_row++;
    }

    fclose(fp);
} 

void print_board(char *title, Board board, BoardType type) {

    /*
        Helper function to print the board.
    */
    
    char buffer[MAX_BUFFER_SIZE * 2];
    snprintf(buffer, sizeof(buffer), "\n# --- %s --- #\n", title);

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            if (type == BOARD) 
                snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d ", board.grid[i * board.cols_count + j]);
            else
                if (board.solution[i * board.cols_count + j] == WHITE) 
                    strncat(buffer, "O ", sizeof(buffer) - strlen(buffer) - 1);
                else if (board.solution[i * board.cols_count + j] == BLACK) 
                    strncat(buffer, "X ", sizeof(buffer) - strlen(buffer) - 1);
                else 
                    strncat(buffer, "? ", sizeof(buffer) - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }
    strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    printf("%s", buffer);
}

bool is_board_solution_equal(Board first_board, Board second_board) {
    /*
        Helper function to check if two boards are equal.
    */

    if (first_board.rows_count != second_board.rows_count || first_board.cols_count != second_board.cols_count) return false;

    int i, j;
    for (i = 0; i < first_board.rows_count; i++)
        for (j = 0; j < first_board.cols_count; j++)
            if (first_board.solution[i * first_board.cols_count + j] != second_board.solution[i * first_board.cols_count + j]) return false;

    return true;
}

Board transpose(Board board) {
    /*
        Helper function to transpose a matrix.
    */

    Board Tboard = { (int *) malloc(board.rows_count * board.cols_count * sizeof(int)), board.cols_count, board.rows_count, (int *) malloc(board.rows_count * board.cols_count * sizeof(int)) };

    int i, j;
    for (i = 0; i < board.rows_count; i++) {
        for (j = 0; j < board.cols_count; j++) {
            Tboard.grid[j * board.rows_count + i] = board.grid[i * board.cols_count + j];
            Tboard.solution[j * board.rows_count + i] = board.solution[i * board.cols_count + j];
        }
    }

    return Tboard;
}

Board combine_boards(Board first_board, Board second_board, bool forced, char *technique) {
    
    int rows = -1, cols = -1;

        rows = first_board.rows_count;
        cols = first_board.cols_count;


    /*
        Initialize the solution board with the values of the original board.
    */

    Board merged = { (int *) malloc(rows * cols * sizeof(int)), rows, cols, (int *) malloc(rows * cols * sizeof(int)) };

    merged.grid = first_board.grid;

    /*
        Combine the solutions by performing a pairwise comparison:
        1) If the values are the same, keep the value
        2) If the values are unknown and white, mark the cell as white
        3) If the values are different, mark the cell as black
    */

    memset(merged.solution, UNKNOWN, rows * cols * sizeof(int));
    #pragma omp parallel for collapse(2)
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (first_board.solution[i * cols + j] == second_board.solution[i * cols + j]) 
                merged.solution[i * cols + j] = first_board.solution[i * cols + j];
            else if (!forced && first_board.solution[i * cols + j] == WHITE && second_board.solution[i * cols + j] == UNKNOWN) 
                merged.solution[i * cols + j] = WHITE;
            else if (!forced && first_board.solution[i * cols + j] == UNKNOWN && second_board.solution[i * cols + j] == WHITE) 
                merged.solution[i * cols + j] = WHITE;
            else if (!forced) 
                merged.solution[i * cols + j] = BLACK;
        }   
    }

    return merged;
}
