#ifndef BOARD_H
#define BOARD_H

#include "common.h"

void read_board(Board* board, char *filename);
void print_board(char *title, Board board, BoardType type);
bool is_board_solution_equal(Board first_board, Board second_board);
Board transpose(Board board);
Board combine_boards(Board first_board, Board second_board, bool forced, char *technique);

#endif