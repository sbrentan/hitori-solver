CFLAGS= -Wall -I src/libs

hitori-solver-mpi: src/hitori-solver-mpi.c
	mpicc $(CFLAGS) -o build/hitori-solver-mpi.out src/hitori-solver-mpi.c

clean:
	rm -f build/hitori-solver-mpi.out