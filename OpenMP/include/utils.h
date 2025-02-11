#ifndef UTILS_H
#define UTILS_H

#include "common.h"

void write_solution(Board board);
void print_vector(int *vector, int size);
void print_block(Board board, char *title, BCB* block);
void free_memory(int *pointers[]);

#endif