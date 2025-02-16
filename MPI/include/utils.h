#ifndef UTILS_H
#define UTILS_H

#include <mpi.h>

#include "common.h"

void write_solution(Board board);
void print_vector(int *vector, int size);
void print_block(Board board, char *title, BCB* block);
void free_memory(int *pointers[]);
void mpi_share_board(Board* board, int rank);
void mpi_scatter_board(Board board, int rank, int size, ScatterType scatter_type, BoardType target_type, int **local_vector, int **counts_send, int **displs_send, MPI_Comm PRUNING_COMM);
void mpi_gather_board(Board board, int rank, int *local_vector, int *counts_send, int *displs_send, int **solution, MPI_Comm PRUNING_COMM);

#endif