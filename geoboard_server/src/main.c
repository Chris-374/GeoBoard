/*
 * main.c
 *
 * Punto de entrada del ejecutable geoboard_server_cluster.
 *
 * Este mismo binario funciona como:
 * - servidor/orquestador si el rank global es 1
 * - worker si el rank global es 2 o superior
 *
 * El rank 0 NO debe ejecutar este binario. El rank 0 debe ser el cliente.
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#include "geoboard_protocol.h"
#include "server_orchestrator.h"
#include "worker_node.h"

int main(int argc, char **argv) {
    int rank = -1;
    int world_size = 0;
    int exit_code = EXIT_SUCCESS;

    (void)argv;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (rank == GEOBOARD_SERVER_RANK) {
        exit_code = server_main(world_size);
    } else if (rank >= GEOBOARD_FIRST_WORKER_RANK) {
        exit_code = worker_main(rank);
    } else if (rank == GEOBOARD_CLIENT_RANK) {
        fprintf(stderr, "[INFO] Rank 0 pertenece al cliente. Ejecute con MPMD:\n");
        fprintf(stderr, "       mpirun -np 1 ./geoboard_client imagen.pgm : -np 4 ./geoboard_server_cluster\n");
        exit_code = EXIT_FAILURE;
    } else {
        fprintf(stderr, "[INFO] Rank %d no tiene rol asignado.\n", rank);
        exit_code = EXIT_SUCCESS;
    }

    MPI_Finalize();
    return exit_code;
}
