/*
 * server_orchestrator.c
 *
 * Servidor principal del GeoBoard.
 *
 * Responsabilidades:
 * - Recibir imagen cifrada desde el cliente rank 0.
 * - Guardar el archivo cifrado y el descifrado.
 * - Interpretar PGM P2/P5.
 * - Dividir la imagen en 9 regiones.
 * - Asignar 3 regiones a cada worker.
 * - Cifrar los paquetes enviados a cada worker.
 * - Consolidar resultados parciales.
 * - Generar la mascara final 8x8.
 */

#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chacha20.h"
#include "file_utils.h"
#include "geoboard_protocol.h"
#include "pgm_image.h"
#include "server_orchestrator.h"

static uint32_t region_x0(uint32_t width, uint32_t col) {
    return (width * col) / 3u;
}

static uint32_t region_x1(uint32_t width, uint32_t col) {
    return (width * (col + 1u)) / 3u;
}

static uint32_t region_y0(uint32_t height, uint32_t row) {
    return (height * row) / 3u;
}

static uint32_t region_y1(uint32_t height, uint32_t row) {
    return (height * (row + 1u)) / 3u;
}

/*
 * Construye el payload para un worker.
 *
 * worker_index 0 -> fila superior: regiones 1, 2, 3
 * worker_index 1 -> fila central:  regiones 4, 5, 6
 * worker_index 2 -> fila inferior:  regiones 7, 8, 9
 */
static int build_worker_payload(const PgmImage *image,
                                uint32_t worker_index,
                                WorkerTaskHeader *header,
                                uint8_t **out_payload) {
    uint32_t row = worker_index;
    uint32_t col;
    uint32_t total_size = 0;
    uint32_t payload_offset = 0;
    uint8_t *payload = NULL;

    if (image == NULL || header == NULL || out_payload == NULL ||
        image->pixels == NULL) {
        return -1;
    }

    memset(header, 0, sizeof(*header));

    header->magic = WORKER_TASK_MAGIC;
    header->version = GEOBOARD_VERSION;
    header->worker_index = worker_index;
    header->image_width = image->width;
    header->image_height = image->height;
    header->region_count = 3u;

    /*
     * Counter y nonce para cifrar la comunicacion servidor -> worker.
     */
    header->counter = 1u + worker_index;
    build_worker_nonce(worker_index, header->nonce);

    for (col = 0; col < 3u; col++) {
        uint32_t x0 = region_x0(image->width, col);
        uint32_t x1 = region_x1(image->width, col);
        uint32_t y0 = region_y0(image->height, row);
        uint32_t y1 = region_y1(image->height, row);
        RegionInfo *region = &header->regions[col];

        region->region_id = (row * 3u) + col + 1u;
        region->start_x = x0;
        region->start_y = y0;
        region->width = x1 - x0;
        region->height = y1 - y0;

        total_size += region->width * region->height;
    }

    /*
     * Para aceptar imagenes de cualquier tamano, incluso imagenes muy pequenas
     * como 1x1 o 2x2, se permiten regiones vacias.
     *
     * Ejemplo:
     * - una imagen 1x1 no puede llenar 9 regiones reales;
     * - algunos workers recibiran payload_size = 0;
     * - esos workers devuelven una mascara vacia sin fallar.
     */
    if (total_size == 0) {
        payload = NULL;
    } else {
        payload = (uint8_t *)malloc(total_size);
        if (payload == NULL) {
            fprintf(stderr,
                    "[SERVIDOR] No hay memoria para payload worker %u.\n",
                    worker_index);
            return -1;
        }
    }

    /*
     * Copia los pixeles de las 3 regiones del worker en un bloque lineal.
     */
    for (col = 0; col < 3u; col++) {
        RegionInfo *region = &header->regions[col];
        uint32_t y;

        for (y = 0; y < region->height; y++) {
            uint32_t global_y = region->start_y + y;
            uint32_t source_index = (global_y * image->width) + region->start_x;
            uint32_t copy_size = region->width;

            if (copy_size > 0 && payload != NULL) {
                memcpy(payload + payload_offset,
                       image->pixels + source_index,
                       copy_size);
            }

            payload_offset += copy_size;
        }
    }

    header->payload_size = total_size;
    *out_payload = payload;
    return 0;
}

static int send_worker_task(int worker_rank,
                            const PgmImage *image,
                            uint32_t worker_index) {
    WorkerTaskHeader header;
    uint8_t *payload = NULL;

    if (build_worker_payload(image, worker_index, &header, &payload) != 0) {
        return -1;
    }

    /*
     * Todo lo que viaja del servidor al worker tambien va cifrado.
     */
    if (header.payload_size > 0 && payload != NULL) {
        chacha20_apply(payload,
                       header.payload_size,
                       GEOBOARD_CHACHA20_KEY,
                       header.nonce,
                       header.counter);
    }

    if (MPI_Send(&header,
                 (int)sizeof(header),
                 MPI_BYTE,
                 worker_rank,
                 TAG_WORKER_HEADER,
                 MPI_COMM_WORLD) != MPI_SUCCESS) {
        free(payload);
        return -1;
    }

    /*
     * MPI permite count = 0. Aun asi, se usa un byte dummy para evitar
     * depender de aritmetica con punteros NULL en implementaciones estrictas.
     */
    {
        uint8_t dummy_payload = 0;
        void *send_buffer = (header.payload_size > 0 && payload != NULL)
            ? (void *)payload
            : (void *)&dummy_payload;

        if (MPI_Send(send_buffer,
                     (int)header.payload_size,
                     MPI_BYTE,
                     worker_rank,
                     TAG_WORKER_PAYLOAD,
                     MPI_COMM_WORLD) != MPI_SUCCESS) {
            free(payload);
            return -1;
        }
    }

    printf("[SERVIDOR] Enviadas regiones %u, %u, %u cifradas al worker rank %d.\n
           header.regions[0].region_id,
           header.regions[1].region_id,
           header.regions[2].region_id,
           worker_rank);

    free(payload);
    return 0;
}

static void send_shutdown_to_workers(void) {
    int rank;
    WorkerTaskHeader header;

    memset(&header, 0, sizeof(header));
    header.magic = WORKER_SHUTDOWN_MAGIC;

    for (rank = GEOBOARD_FIRST_WORKER_RANK;
         rank <= GEOBOARD_LAST_WORKER_RANK;
         rank++) {
        MPI_Send(&header,
                 (int)sizeof(header),
                 MPI_BYTE,
                 rank,
                 TAG_WORKER_HEADER,
                 MPI_COMM_WORLD);
    }
}

/*
 * Recibe el archivo cifrado desde el cliente rank 0.
 * El cliente ya envio metadata, nombre y chunks del archivo usando MPI_Send.
 */
static int receive_client_file(uint8_t **out_encrypted_data,
                               uint64_t *out_file_size,
                               char *out_filename,
                               size_t out_filename_size,
                               uint32_t *out_counter,
                               uint8_t out_nonce[CHACHA20_NONCE_SIZE]) {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t file_size = 0;
    uint32_t filename_len = 0;
    uint32_t counter = 0;
    uint32_t chunk_size = 0;
    uint32_t end_marker = 0;
    uint8_t *encrypted_data = NULL;
    uint64_t offset = 0;
    char *received_filename = NULL;

    printf("[SERVIDOR] Esperando metadata del cliente rank %d...\n",
           GEOBOARD_CLIENT_RANK);

    MPI_Recv(&magic, 1, MPI_UINT32_T,
             GEOBOARD_CLIENT_RANK, TAG_MAGIC,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Recv(&version, 1, MPI_UINT32_T,
             GEOBOARD_CLIENT_RANK, TAG_VERSION,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Recv(&file_size, 1, MPI_UINT64_T,
             GEOBOARD_CLIENT_RANK, TAG_FILE_SIZE,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Recv(&filename_len, 1, MPI_UINT32_T,
             GEOBOARD_CLIENT_RANK, TAG_FILENAME_LEN,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Recv(&counter, 1, MPI_UINT32_T,
             GEOBOARD_CLIENT_RANK, TAG_COUNTER,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Recv(&chunk_size, 1, MPI_UINT32_T,
             GEOBOARD_CLIENT_RANK, TAG_CHUNK_SIZE,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Recv(out_nonce, CHACHA20_NONCE_SIZE, MPI_BYTE,
             GEOBOARD_CLIENT_RANK, TAG_NONCE,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (magic != GEOBOARD_MAGIC) {
        fprintf(stderr, "[SERVIDOR] Magic invalido recibido del cliente.\n");
        return -1;
    }

    if (version != GEOBOARD_VERSION) {
        fprintf(stderr,
                "[SERVIDOR] Version incompatible. Recibida %u, esperada %u.\n",
                version, GEOBOARD_VERSION);
        return -1;
    }

    if (file_size == 0 || chunk_size == 0 || filename_len == 0) {
        fprintf(stderr, "[SERVIDOR] Metadata invalida.\n");
        return -1;
    }

    if (chunk_size > GEOBOARD_CHUNK_SIZE) {
        fprintf(stderr, "[SERVIDOR] Chunk size muy grande.\n");
        return -1;
    }

    received_filename = (char *)malloc((size_t)filename_len + 1u);
    if (received_filename == NULL) {
        fprintf(stderr, "[SERVIDOR] No hay memoria para nombre de archivo.\n");
        return -1;
    }

    MPI_Recv(received_filename,
             (int)filename_len,
             MPI_CHAR,
             GEOBOARD_CLIENT_RANK,
             TAG_FILENAME,
             MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    received_filename[filename_len] = '\0';

    sanitize_filename(received_filename, out_filename, out_filename_size);
    free(received_filename);

    encrypted_data = (uint8_t *)malloc((size_t)file_size);
    if (encrypted_data == NULL) {
        fprintf(stderr, "[SERVIDOR] No hay memoria para imagen cifrada.\n");
        return -1;
    }

    printf("[SERVIDOR] Recibiendo archivo cifrado '%s' de %llu bytes...\n",
           out_filename,
           (unsigned long long)file_size);

    while (offset < file_size) {
        uint64_t remaining = file_size - offset;
        int current_chunk = (remaining > chunk_size)
            ? (int)chunk_size
            : (int)remaining;

        MPI_Recv(encrypted_data + offset,
                 current_chunk,
                 MPI_BYTE,
                 GEOBOARD_CLIENT_RANK,
                 TAG_FILE_CHUNK,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        offset += (uint64_t)current_chunk;
    }

    MPI_Recv(&end_marker,
             1,
             MPI_UINT32_T,
             GEOBOARD_CLIENT_RANK,
             TAG_END_OF_FILE,
             MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);

    if (end_marker != 1u) {
        fprintf(stderr, "[SERVIDOR] Marca de fin de archivo invalida.\n");
        free(encrypted_data);
        return -1;
    }

    *out_encrypted_data = encrypted_data;
    *out_file_size = file_size;
    *out_counter = counter;

    printf("[SERVIDOR] Archivo recibido correctamente.\n");
    return 0;
}

/*
 * Clasificacion muy simple para tener una salida defendible.
 * No pretende ser vision por computadora avanzada, solo una metrica inicial.
 */
static const char *classify_basic_shape(uint64_t active_pixels,
                                        int32_t bbox_min_x,
                                        int32_t bbox_min_y,
                                        int32_t bbox_max_x,
                                        int32_t bbox_max_y) {
    int32_t bbox_w;
    int32_t bbox_h;
    double aspect;
    double bbox_area;
    double fill_ratio;

    if (active_pixels == 0 || bbox_min_x < 0 || bbox_min_y < 0) {
        return "sin figura detectada";
    }

    bbox_w = bbox_max_x - bbox_min_x + 1;
    bbox_h = bbox_max_y - bbox_min_y + 1;

    if (bbox_w <= 0 || bbox_h <= 0) {
        return "figura desconocida";
    }

    aspect = (double)bbox_w / (double)bbox_h;
    bbox_area = (double)bbox_w * (double)bbox_h;
    fill_ratio = (double)active_pixels / bbox_area;

    if (aspect >= 0.85 && aspect <= 1.15 && fill_ratio >= 0.55) {
        return "cuadrado o circulo aproximado";
    }

    if ((aspect < 0.85 || aspect > 1.15) && fill_ratio >= 0.50) {
        return "rectangulo";
    }

    if (fill_ratio >= 0.25 && fill_ratio < 0.55) {
        return "triangulo, linea o figura hueca";
    }

    return "figura geometrica simple";
}

int server_main(int world_size) {
    uint8_t *encrypted_data = NULL;
    uint8_t *decrypted_data = NULL;
    uint64_t file_size = 0;
    uint32_t counter = 0;
    uint8_t nonce[CHACHA20_NONCE_SIZE];

    char filename[256];
    char encrypted_path[512];
    char decrypted_path[512];
    char mask_path[512];

    PgmImage image;
    uint8_t final_mask[8];

    uint64_t total_active = 0;
    uint64_t total_edges = 0;

    int32_t global_min_x = -1;
    int32_t global_min_y = -1;
    int32_t global_max_x = -1;
    int32_t global_max_y = -1;

    int worker_index;

    memset(filename, 0, sizeof(filename));
    memset(nonce, 0, sizeof(nonce));
    memset(&image, 0, sizeof(image));
    memset(final_mask, 0, sizeof(final_mask));

    if (world_size < 5) {
        fprintf(stderr,
                "[SERVIDOR] Se necesitan 5 ranks globales: cliente, servidor y 3 workers.\n");
        fprintf(stderr,
                "[SERVIDOR] Ejemplo: mpirun -np 1 ./geoboard_client imagen.pgm : -np 4 ./geoboard_server_cluster\n");
        return EXIT_FAILURE;
    }

    if (ensure_output_dir() != 0) {
        send_shutdown_to_workers();
        return EXIT_FAILURE;
    }

    if (receive_client_file(&encrypted_data,
                            &file_size,
                            filename,
                            sizeof(filename),
                            &counter,
                            nonce) != 0) {
        send_shutdown_to_workers();
        return EXIT_FAILURE;
    }

    snprintf(encrypted_path,
             sizeof(encrypted_path),
             "%s/encrypted_%s.bin",
             OUTPUT_DIR,
             filename);

    snprintf(decrypted_path,
             sizeof(decrypted_path),
             "%s/decrypted_%s",
             OUTPUT_DIR,
             filename);

    snprintf(mask_path,
             sizeof(mask_path),
             "%s/mask8x8_%s.txt",
             OUTPUT_DIR,
             filename);

    write_file(encrypted_path, encrypted_data, file_size);

    decrypted_data = (uint8_t *)malloc((size_t)file_size);
    if (decrypted_data == NULL) {
        fprintf(stderr, "[SERVIDOR] No hay memoria para descifrar imagen.\n");
        free(encrypted_data);
        send_shutdown_to_workers();
        return EXIT_FAILURE;
    }

    /*
     * Descifrado de imagen recibida del cliente.
     * La key es compartida; nonce y counter vinieron en metadata.
     */
    memcpy(decrypted_data, encrypted_data, (size_t)file_size);
    chacha20_apply(decrypted_data,
                   file_size,
                   GEOBOARD_CHACHA20_KEY,
                   nonce,
                   counter);

    write_file(decrypted_path, decrypted_data, file_size);

    printf("[SERVIDOR] Archivo cifrado guardado en: %s\n", encrypted_path);
    printf("[SERVIDOR] Archivo descifrado guardado en: %s\n", decrypted_path);

    if (parse_pgm_image(decrypted_data, file_size, &image) != 0) {
        fprintf(stderr,
                "[SERVIDOR] No se pudo interpretar la imagen. Use PGM P2 o P5.\n");
        free(encrypted_data);
        free(decrypted_data);
        send_shutdown_to_workers();
        return EXIT_FAILURE;
    }

    printf("[SERVIDOR] Imagen PGM cargada: %ux%u, max=%u\n",
           image.width,
           image.height,
           image.max_value);
    printf("[SERVIDOR] La imagen puede tener cualquier resolucion positiva; se divide proporcionalmente en 3x3 y se reduce a 8x8.\n");

    /*
     * Se envian 3 tareas:
     * - rank 2 procesa regiones 1, 2, 3
     * - rank 3 procesa regiones 4, 5, 6
     * - rank 4 procesa regiones 7, 8, 9
     */
    for (worker_index = 0; worker_index < GEOBOARD_WORKER_COUNT; worker_index++) {
        int worker_rank = GEOBOARD_FIRST_WORKER_RANK + worker_index;

        if (send_worker_task(worker_rank,
                             &image,
                             (uint32_t)worker_index) != 0) {
            fprintf(stderr,
                    "[SERVIDOR] Error enviando tarea al worker rank %d.\n",
                    worker_rank);

            free_pgm_image(&image);
            free(encrypted_data);
            free(decrypted_data);
            send_shutdown_to_workers();
            return EXIT_FAILURE;
        }
    }

    /*
     * Recepcion y consolidacion de resultados.
     */
    for (worker_index = 0; worker_index < GEOBOARD_WORKER_COUNT; worker_index++) {
        WorkerResult result;
        int y;

        memset(&result, 0, sizeof(result));

        MPI_Recv(&result,
                 (int)sizeof(result),
                 MPI_BYTE,
                 GEOBOARD_FIRST_WORKER_RANK + worker_index,
                 TAG_WORKER_RESULT,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        if (result.magic != WORKER_RESULT_MAGIC) {
            fprintf(stderr,
                    "[SERVIDOR] Resultado invalido del worker %d.\n",
                    worker_index);
            continue;
        }

        printf("[SERVIDOR] Resultado worker %u: active=%llu, edges=%llu\n",
               result.worker_index,
               (unsigned long long)result.active_pixels,
               (unsigned long long)result.edge_pixels);

        for (y = 0; y < 8; y++) {
            final_mask[y] |= result.mask[y];
        }

        total_active += result.active_pixels;
        total_edges += result.edge_pixels;

        if (result.has_content) {
            if (global_min_x < 0 || result.bbox_min_x < global_min_x) {
                global_min_x = result.bbox_min_x;
            }

            if (global_min_y < 0 || result.bbox_min_y < global_min_y) {
                global_min_y = result.bbox_min_y;
            }

            if (global_max_x < 0 || result.bbox_max_x > global_max_x) {
                global_max_x = result.bbox_max_x;
            }

            if (global_max_y < 0 || result.bbox_max_y > global_max_y) {
                global_max_y = result.bbox_max_y;
            }
        }
    }

    print_mask(final_mask);
    save_mask_file(mask_path, final_mask);

    printf("[SERVIDOR] Mascara final guardada en: %s\n", mask_path);
    printf("[SERVIDOR] Pixeles activos globales: %llu\n",
           (unsigned long long)total_active);
    printf("[SERVIDOR] Bordes aproximados globales: %llu\n",
           (unsigned long long)total_edges);
    printf("[SERVIDOR] Bounding box global: (%d,%d) -> (%d,%d)\n",
           global_min_x,
           global_min_y,
           global_max_x,
           global_max_y);
    printf("[SERVIDOR] Clasificacion basica: %s\n",
           classify_basic_shape(total_active,
                                global_min_x,
                                global_min_y,
                                global_max_x,
                                global_max_y));

    printf("[SERVIDOR] Procesamiento distribuido terminado.\n");
    printf("[SERVIDOR] Siguiente capa pendiente: enviar mask8x8 a libgeoboard.a y luego al driver GPIO.\n");

    free_pgm_image(&image);
    free(encrypted_data);
    free(decrypted_data);

    return EXIT_SUCCESS;
}
