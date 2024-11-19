CFLAGS= -Wall -lm

hitori-solver-mpi: hitori-solver-mpi.c
	mpicc $(CFLAGS) -o hitori-solver-mpi.out hitori-solver-mpi.c

clean:
	rm -f hitori-solver-mpi.out