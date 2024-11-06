#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_BUFFER_SIZE 2048
#define DEBUG true

int rows_count = 0, cols_count = 0; // Number of rows and columns in the puzzle

int * board; // Original puzzle board
bool * solution; // Solution matrix: false - white, true - black

int i, j;

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
    solution = malloc((rows_count * cols_count) * sizeof(bool));

    rewind(fp);

    int rows = 0, cols;
    while (fgets(line, MAX_BUFFER_SIZE, fp)) {
        char *token = strtok(line, " ");

        cols = 0;
        do {  
            board[rows * cols_count + cols] = atoi(token);
            solution[rows * cols_count + cols] = false;
            cols++;
        } while (token = strtok(NULL, " "));

        rows++;
    }

    fclose(fp);
} 

// Helper function to print a matrix
void print_board() {

    if(DEBUG) printf("\nRows: %d, Cols: %d\n", rows_count, cols_count);

    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            printf("%d ", board[i * cols_count + j]);
        }
        printf("\n");
    }

    fflush(stdout);
}

void print_bool_matrix(bool * matrix) {

    if(DEBUG) printf("\nRows: %d, Cols: %d\n", rows_count, cols_count);

    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            if (matrix[i * cols_count + j]) 
                printf("X ");
            else
                printf("O ");
        }
        printf("\n");
    }

    fflush(stdout);
}

bool has_unique_values() {
    
    // Rule 1: No unshaded number appears in a row or column more than once
    int k;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            if (!solution[i * cols_count + j]) {
                for (k = 0; k < rows_count; k++) {
                    if (k != i && !solution[k * cols_count + j] && board[i * cols_count + j] == board[k * cols_count + j]) return false;
                }

                for (k = 0; k < cols_count; k++) {
                    if (k != j && !solution[i * cols_count + k] && board[i * cols_count + j] == board[i * cols_count + k]) return false;
                }
            }
        }
    }

    return true;
}

bool is_valid_blackout(int row, int col) {

    if (solution[row * cols_count + col]) return false;

    // Rule 2: Shaded (black) squares do not touch each other vertically or horizontally
    if (row > 0 && solution[(row - 1) * cols_count + col]) return false; // Check above
    if (row < rows_count - 1 && solution[(row + 1) * cols_count + col]) return false; // Check below
    if (col > 0 && solution[row * cols_count + col - 1]) return false; // Check left
    if (col < cols_count - 1 && solution[row * cols_count + col + 1]) return false; // Check right   

    return true;
}

int dfs_white_cells(bool * visited, int row, int col) {
    if (row < 0 || row >= rows_count || col < 0 || col >= cols_count || visited[row * cols_count + col] || solution[row * cols_count + col]) return 0;
    visited[row * cols_count + col] = true;

    int count = 1;
    count += dfs_white_cells(visited, row - 1, col);
    count += dfs_white_cells(visited, row + 1, col);
    count += dfs_white_cells(visited, row, col - 1);
    count += dfs_white_cells(visited, row, col + 1);
    return count;
}

bool all_white_cells_connected() {

    bool * visited = malloc((rows_count * cols_count) * sizeof(bool));

    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            visited[i * cols_count + j] = false;
        }
    }

    // Find the first white cell
    int row = -1, col = -1;

    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            if (!solution[i * cols_count + j]) {
                row = i;
                col = j;
                break;
            }
        }
        if (row != -1) break;
    }

    int white_cells_count = 0;
    for (i = 0; i < rows_count; i++) {
        for (j = 0; j < cols_count; j++) {
            if (!solution[i * cols_count + j]) white_cells_count++;
        }
    }

    // Rule 3: When completed, all un-shaded (white) squares create a single continuous area
    return dfs_white_cells(visited, row, col) == white_cells_count;
}

// Recursive backtracking solving function
bool solveHitori(int row, int col) {
    if (col == cols_count) { col = 0; row++; } // Move to the next row
    if (row == rows_count) return has_unique_values() && all_white_cells_connected();

    // Try shading the cell as black
    if (is_valid_blackout(row, col)) {
        
        // Try shading the cell
        solution[row * cols_count + col] = true;

        // Check if the solution is still valid in the next cell
        if (solveHitori(row, col + 1)) return true;

        // If not, backtrack
        solution[row * cols_count + col] = false;
    }

    // Try leaving the cell as white
    return solveHitori(row, col + 1);
}

int main() {
    
    read_board();

    print_board();
    
    if (solveHitori(0, 0)) {
        printf("\nSolution found!\n");
        print_bool_matrix(solution);
    } else {
        printf("\nNo solution found!\n");
    }

    return 0;
}