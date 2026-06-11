/*
 * worker_node.c
 *
 * Codigo de los nodos de procesamiento.
 *
 * Cada worker:
 * 1. Espera una tarea del servidor.
 * 2. Recibe header y payload CIFRADO.
 * 3. Descifra localmente su franja usando ChaCha20 y el offset original.
 * 4. Reorganiza la franja en las 3 regiones que le corresponden.
 * 5. Procesa sus 3 regiones.
 * 6. Devuelve un WorkerResult al servidor.
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

/*
 * El payload recibido ahora es una franja completa de filas de la imagen,
 * no las 3 regiones compactadas.
 *
 * Esta funcion crea un buffer compacto con las 3 regiones concatenadas en el
 * formato que ya entiende process_worker_regions().
 */
static int build_compact_regions_from_decrypted_stripe(const WorkerTaskHeader *header,
                                                       const uint8_t *stripe_payload,
                                                       uint8_t **out_compact_payload,
                                                       uint32_t *out_compact_size) {
    uint32_t region_index;
    uint32_t total_size = 0;
    uint32_t compact_offset = 0;
    uint8_t *compact = NULL;

    if (header == NULL || out_compact_payload == NULL || out_compact_size == NULL) {
        return -1;
    }

    for (region_index = 0; region_index < header->region_count; region_index++) {
        const RegionInfo *region = &header->regions[region_index];
        total_size += region->width * region->height;
    }

    if (total_size == 0) {
        *out_compact_payload = NULL;
        *out_compact_size = 0;
        return 0;
    }

    if (stripe_payload == NULL) {
        return -1;
    }

    compact = (uint8_t *)malloc(total_size);
    if (compact == NULL) {
        fprintf(stderr, "[WORKER] No hay memoria para compactar regiones.\n");
        return -1;
    }

    for (region_index = 0; region_index < header->region_count; region_index++) {
        const RegionInfo *region = &header->regions[region_index];
        uint32_t y;

        for (y = 0; y < region->height; y++) {
            uint32_t global_y = region->start_y + y;
            uint32_t local_y = global_y - header->stripe_start_y;
            uint32_t stripe_index = (local_y * header->image_width) + region->start_x;
            uint32_t copy_size = region->width;

            if (copy_size > 0) {
                memcpy(compact + compact_offset,
                       stripe_payload + stripe_index,
                       copy_size);
            }

            compact_offset += copy_size;
        }
    }

    *out_compact_payload = compact;
    *out_compact_size = total_size;
    return 0;
}

int worker_main(int rank) {
    WorkerTaskHeader header;
    uint8_t *payload = NULL;
    uint8_t *compact_payload = NULL;
    uint32_t compact_size = 0;
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

    if (header.region_count == 0u || header.region_count > GEOBOARD_MAX_REGIONS_PER_WORKER) {
        fprintf(stderr, "[WORKER %d] Tarea invalida. region_count=%u\n", rank, header.region_count);
        return EXIT_FAILURE;
    }

    if (header.payload_size > 0u) {
        payload = (uint8_t *)malloc(header.payload_size);
        if (payload == NULL) {
            fprintf(stderr, "[WORKER %d] No hay memoria para payload.\n", rank);
            return EXIT_FAILURE;
        }
    } else {
        payload = NULL;
    }

    {
        uint8_t dummy_payload = 0;
        void *recv_buffer = (header.payload_size > 0u && payload != NULL)
            ? (void *)payload
            : (void *)&dummy_payload;

        MPI_Recv(recv_buffer,
                 (int)header.payload_size,
                 MPI_BYTE,
                 GEOBOARD_SERVER_RANK,
                 TAG_WORKER_PAYLOAD,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
    }

    /*
     * El payload recibido es una franja del archivo cifrado original.
     * Se descifra en el worker usando el offset absoluto dentro del archivo.
     */
    if (header.payload_size > 0u && payload != NULL) {
        chacha20_apply_with_offset(payload,
                                   header.payload_size,
                                   GEOBOARD_CHACHA20_KEY,
                                   header.nonce,
                                   header.counter,
                                   header.payload_file_offset);

        printf("[WORKER %d] Franja cifrada descifrada localmente. file_offset=%llu bytes\n",
               rank,
               (unsigned long long)header.payload_file_offset);
    }

    if (build_compact_regions_from_decrypted_stripe(&header,
                                                    payload,
                                                    &compact_payload,
                                                    &compact_size) != 0) {
        fprintf(stderr, "[WORKER %d] Error compactando regiones desde franja descifrada.\n", rank);
        free(payload);
        return EXIT_FAILURE;
    }

    {
        uint32_t heavy_passes = geoboard_get_processing_passes();
        double t0;
        double t1;

        uint32_t region_i;

        printf("[WORKER %d] Procesando %u regiones con GEOBOARD_HEAVY_PASSES=%u: ",
               rank,
               header.region_count,
               heavy_passes);

        for (region_i = 0; region_i < header.region_count; region_i++) {
            printf("%u", header.regions[region_i].region_id);
            if (region_i + 1u < header.region_count) {
                printf(",");
            }
        }
        printf("\n");

        t0 = MPI_Wtime();

        {
            uint8_t dummy_payload = 0;
            const uint8_t *processing_payload = (compact_payload != NULL)
                ? compact_payload
                : &dummy_payload;

            process_worker_regions(&header, processing_payload, &result);
        }

        t1 = MPI_Wtime();

        printf("[WORKER %d] Tiempo de procesamiento local: %.3f s\n",
               rank,
               t1 - t0);
    }

    MPI_Send(&result,
             (int)sizeof(result),
             MPI_BYTE,
             GEOBOARD_SERVER_RANK,
             TAG_WORKER_RESULT,
             MPI_COMM_WORLD);

    printf("[WORKER %d] Resultado enviado. active=%llu edge_metric=%llu\n",
           rank,
           (unsigned long long)result.active_pixels,
           (unsigned long long)result.edge_pixels);

    free(payload);
    free(compact_payload);
    (void)compact_size;
    return EXIT_SUCCESS;
}
