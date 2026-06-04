/*
 * worker_node.c
 *
 * Codigo de los nodos de procesamiento.
 *
 * Cada worker:
 * 1. Espera una tarea del servidor.
 * 2. Recibe header y payload cifrado.
 * 3. Descifra el payload con ChaCha20.
 * 4. Procesa sus 3 regiones.
 * 5. Devuelve un WorkerResult al servidor.
 */

#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chacha20.h"
#include "geoboard_protocol.h"
#include "worker_node.h"
#include "worker_processing.h"

int worker_main(int rank) {
    WorkerTaskHeader header;
    uint8_t *payload = NULL;
    WorkerResult result;

    memset(&header, 0, sizeof(header));
    memset(&result, 0, sizeof(result));

    printf("[WORKER %d] Esperando tarea del servidor rank %d...\n",
           rank, GEOBOARD_SERVER_RANK);

    MPI_Recv(&header,
             (int)sizeof(header),
             MPI_BYTE,
             GEOBOARD_SERVER_RANK,
             TAG_WORKER_HEADER,
             MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    if (header.magic == WORKER_SHUTDOWN_MAGIC) {
        printf("[WORKER %d] Shutdown recibido.\n", rank);
        return EXIT_SUCCESS;
    }

    if (header.magic != WORKER_TASK_MAGIC) {
        fprintf(stderr, "[WORKER %d] Header de tarea invalido.\n", rank);
        return EXIT_FAILURE;
    }

    if (header.region_count != 3u || header.payload_size == 0u) {
        fprintf(stderr, "[WORKER %d] Tarea invalida.\n", rank);
        return EXIT_FAILURE;
    }

    payload = (uint8_t *)malloc(header.payload_size);
    if (payload == NULL) {
        fprintf(stderr, "[WORKER %d] No hay memoria para payload.\n", rank);
        return EXIT_FAILURE;
    }

    MPI_Recv(payload,
             (int)header.payload_size,
             MPI_BYTE,
             GEOBOARD_SERVER_RANK,
             TAG_WORKER_PAYLOAD,
             MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    /*
     * El payload recibido viene cifrado.
     * ChaCha20 es simetrico, asi que aplicar chacha20_apply() descifra.
     */
    chacha20_apply(payload,
                   header.payload_size,
                   GEOBOARD_CHACHA20_KEY,
                   header.nonce,
                   header.counter);

    printf("[WORKER %d] Procesando regiones %u, %u, %u...\n",
           rank,
           header.regions[0].region_id,
           header.regions[1].region_id,
           header.regions[2].region_id);

    process_worker_regions(&header, payload, &result);

    MPI_Send(&result,
             (int)sizeof(result),
             MPI_BYTE,
             GEOBOARD_SERVER_RANK,
             TAG_WORKER_RESULT,
             MPI_COMM_WORLD);

    printf("[WORKER %d] Resultado enviado. active=%llu edges=%llu\n",
           rank,
           (unsigned long long)result.active_pixels,
           (unsigned long long)result.edge_pixels);

    free(payload);
    return EXIT_SUCCESS;
}
