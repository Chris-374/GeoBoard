/*
 * server_orchestrator.c
 *
 * Servidor principal del GeoBoard.
 *
 * Cambio importante de esta version:
 * - El servidor recibe la imagen CIFRADA desde el cliente.
 * - El servidor guarda el archivo cifrado.
 * - El servidor descifra SOLO el header PGM necesario para conocer ancho,
 *   alto y offset donde empiezan los pixeles.
 * - El servidor NO descifra todos los pixeles.
 * - El servidor divide la imagen en 3 franjas horizontales CIFRADAS.
 * - Cada worker recibe su franja cifrada y la descifra localmente.
 *
 * Esto calza mejor con el enunciado: la informacion que viaja por red entre
 * servidor y workers sigue cifrada, y los nodos de procesamiento son quienes
 * descifran su parte.
 */

#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chacha20.h"
#include "file_utils.h"
#include "geoboard_protocol.h"
#include "pgm_image.h"
#include "server_orchestrator.h"
#include "geoboard.h"

#define HEADER_DECRYPT_INITIAL_SIZE 4096u
#define HEADER_DECRYPT_MAX_SIZE     1048576u

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
 * Descifra solo un prefijo pequeno del archivo para leer el header PGM.
 * No se descifran los pixeles completos en el servidor.
 */
static int parse_encrypted_pgm_metadata(const uint8_t *encrypted_data,
                                        uint64_t file_size,
                                        uint32_t counter,
                                        const uint8_t nonce[CHACHA20_NONCE_SIZE],
                                        PgmMetadata *metadata) {
    uint64_t attempt_size = HEADER_DECRYPT_INITIAL_SIZE;

    if (encrypted_data == NULL || metadata == NULL || file_size == 0) {
        return -1;
    }

    while (attempt_size <= HEADER_DECRYPT_MAX_SIZE && attempt_size <= file_size) {
        uint8_t *header_copy = (uint8_t *)malloc((size_t)attempt_size);

        if (header_copy == NULL) {
            fprintf(stderr, "[SERVIDOR] No hay memoria para descifrar header PGM.\n");
            return -1;
        }

        memcpy(header_copy, encrypted_data, (size_t)attempt_size);

        chacha20_apply(header_copy,
                       attempt_size,
                       GEOBOARD_CHACHA20_KEY,
                       nonce,
                       counter);

        if (parse_pgm_metadata(header_copy, attempt_size, metadata) == 0) {
            free(header_copy);

            if (metadata->pixel_data_offset + metadata->pixel_data_size > file_size) {
                fprintf(stderr, "[SERVIDOR] Metadata PGM inconsistente con tamano de archivo.\n");
                return -1;
            }

            return 0;
        }

        free(header_copy);
        attempt_size *= 2u;
    }

    fprintf(stderr,
            "[SERVIDOR] No se pudo leer metadata PGM. Use PGM P5 binario con header pequeno.\n");
    return -1;
}

/*
 * Construye el header y payload cifrado para un worker.
 *
 * En vez de mandar pixeles descifrados, se copia una franja CIFRADA directamente
 * desde el archivo cifrado recibido del cliente.
 */
static int build_worker_payload_from_encrypted(const uint8_t *encrypted_data,
                                               uint64_t file_size,
                                               const PgmMetadata *metadata,
                                               uint32_t worker_index,
                                               uint32_t worker_count,
                                               uint32_t client_counter,
                                               const uint8_t client_nonce[CHACHA20_NONCE_SIZE],
                                               WorkerTaskHeader *header,
                                               uint8_t **out_payload) {
    uint32_t region_row_start;
    uint32_t region_row_end;
    uint32_t region_row;
    uint32_t col;
    uint32_t y0;
    uint32_t y1;
    uint32_t stripe_height;
    uint64_t payload_file_offset;
    uint64_t payload_size64;
    uint8_t *payload = NULL;

    if (encrypted_data == NULL || metadata == NULL || header == NULL || out_payload == NULL) {
        return -1;
    }

    if (worker_count == 0u || worker_count > GEOBOARD_MAX_REGIONS_PER_WORKER) {
        fprintf(stderr, "[SERVIDOR] worker_count invalido: %u\n", worker_count);
        return -1;
    }

    memset(header, 0, sizeof(*header));

    /*
     * La malla logica sigue siendo 3x3. En modo normal hay 3 workers y cada
     * uno toma una fila de la malla. En modo failover puede haber 2 workers;
     * entonces se reagrupan filas completas de la malla entre los sobrevivientes.
     */
    region_row_start = (worker_index * 3u) / worker_count;
    region_row_end = ((worker_index + 1u) * 3u) / worker_count;

    if (region_row_end <= region_row_start) {
        region_row_end = region_row_start + 1u;
    }

    if (region_row_end > 3u) {
        region_row_end = 3u;
    }

    y0 = region_y0(metadata->height, region_row_start);
    y1 = region_y1(metadata->height, region_row_end - 1u);
    stripe_height = y1 - y0;

    payload_file_offset = metadata->pixel_data_offset + ((uint64_t)y0 * metadata->width);
    payload_size64 = (uint64_t)metadata->width * stripe_height;

    if (payload_size64 > UINT32_MAX) {
        fprintf(stderr, "[SERVIDOR] Payload demasiado grande para el header actual.\n");
        return -1;
    }

    if (payload_file_offset + payload_size64 > file_size) {
        fprintf(stderr, "[SERVIDOR] Rango cifrado del worker fuera del archivo.\n");
        return -1;
    }

    header->magic = WORKER_TASK_MAGIC;
    header->version = GEOBOARD_VERSION;
    header->worker_index = worker_index;
    header->image_width = metadata->width;
    header->image_height = metadata->height;
    header->payload_size = (uint32_t)payload_size64;

    /*
     * Se reutiliza el counter y nonce originales del cliente, porque el payload
     * es una parte del ciphertext original de la imagen completa.
     */
    header->counter = client_counter;
    memcpy(header->nonce, client_nonce, CHACHA20_NONCE_SIZE);
    header->payload_file_offset = payload_file_offset;
    header->stripe_start_y = y0;
    header->stripe_height = stripe_height;

    header->region_count = 0u;

    for (region_row = region_row_start; region_row < region_row_end; region_row++) {
        uint32_t ry0 = region_y0(metadata->height, region_row);
        uint32_t ry1 = region_y1(metadata->height, region_row);

        for (col = 0; col < 3u; col++) {
            uint32_t x0 = region_x0(metadata->width, col);
            uint32_t x1 = region_x1(metadata->width, col);
            RegionInfo *region;

            if (header->region_count >= GEOBOARD_MAX_REGIONS_PER_WORKER) {
                fprintf(stderr, "[SERVIDOR] Demasiadas regiones para un worker.\n");
                return -1;
            }

            region = &header->regions[header->region_count];
            region->region_id = (region_row * 3u) + col + 1u;
            region->start_x = x0;
            region->start_y = ry0;
            region->width = x1 - x0;
            region->height = ry1 - ry0;
            header->region_count++;
        }
    }

    if (payload_size64 > 0) {
        payload = (uint8_t *)malloc((size_t)payload_size64);
        if (payload == NULL) {
            fprintf(stderr, "[SERVIDOR] No hay memoria para payload cifrado worker %u.\n", worker_index);
            return -1;
        }

        memcpy(payload,
               encrypted_data + payload_file_offset,
               (size_t)payload_size64);
    }

    *out_payload = payload;
    return 0;
}

static int send_worker_task(int worker_rank,
                            const uint8_t *encrypted_data,
                            uint64_t file_size,
                            const PgmMetadata *metadata,
                            uint32_t worker_index,
                            uint32_t worker_count,
                            uint32_t client_counter,
                            const uint8_t client_nonce[CHACHA20_NONCE_SIZE]) {
    WorkerTaskHeader header;
    uint8_t *payload = NULL;

    if (build_worker_payload_from_encrypted(encrypted_data,
                                            file_size,
                                            metadata,
                                            worker_index,
                                            worker_count,
                                            client_counter,
                                            client_nonce,
                                            &header,
                                            &payload) != 0) {
        return -1;
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

    {
        uint32_t i;
        printf("[SERVIDOR] Enviadas %u regiones al worker rank %d como franja CIFRADA original: ",
               header.region_count,
               worker_rank);

        for (i = 0; i < header.region_count; i++) {
            printf("%u", header.regions[i].region_id);
            if (i + 1u < header.region_count) {
                printf(",");
            }
        }
        printf("\n");
    }

    free(payload);
    return 0;
}

static void send_shutdown_to_workers(uint32_t worker_count) {
    uint32_t worker_index;
    WorkerTaskHeader header;

    memset(&header, 0, sizeof(header));
    header.magic = WORKER_SHUTDOWN_MAGIC;

    for (worker_index = 0; worker_index < worker_count; worker_index++) {
        int rank = GEOBOARD_FIRST_WORKER_RANK + (int)worker_index;
        MPI_Send(&header,
                 (int)sizeof(header),
                 MPI_BYTE,
                 rank,
                 TAG_WORKER_HEADER,
                 MPI_COMM_WORLD);
    }
}

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

    MPI_Recv(&magic, 1, MPI_UINT32_T, GEOBOARD_CLIENT_RANK, TAG_MAGIC, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&version, 1, MPI_UINT32_T, GEOBOARD_CLIENT_RANK, TAG_VERSION, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&file_size, 1, MPI_UINT64_T, GEOBOARD_CLIENT_RANK, TAG_FILE_SIZE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&filename_len, 1, MPI_UINT32_T, GEOBOARD_CLIENT_RANK, TAG_FILENAME_LEN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&counter, 1, MPI_UINT32_T, GEOBOARD_CLIENT_RANK, TAG_COUNTER, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&chunk_size, 1, MPI_UINT32_T, GEOBOARD_CLIENT_RANK, TAG_CHUNK_SIZE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(out_nonce, CHACHA20_NONCE_SIZE, MPI_BYTE, GEOBOARD_CLIENT_RANK, TAG_NONCE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (magic != GEOBOARD_MAGIC) {
        fprintf(stderr, "[SERVIDOR] Magic invalido recibido del cliente.\n");
        return -1;
    }

    if (version != GEOBOARD_VERSION) {
        fprintf(stderr,
                "[SERVIDOR] Version incompatible. Recibida %u, esperada %u.\n",
                version,
                GEOBOARD_VERSION);
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
 * Mini juego del GeoBoard.
 *
 * La mascara generada por MPI usa bit 7 como columna izquierda para que la
 * impresion en consola sea legible. El driver usa bit 0 como x=0, por eso se
 * invierte cada fila antes de dibujar o comparar contra la respuesta del
 * usuario.
 *
 * Variables utiles para la demo:
 *   GEOBOARD_SKIP_HARDWARE=1       -> no intenta abrir /dev/geoboard
 *   GEOBOARD_SKIP_GAME=1           -> solo muestra la figura, no entra al juego
 *   GEOBOARD_HW_SECONDS=N          -> tiempo que muestra la figura, default 10
 *   GEOBOARD_TOLERANCE=N           -> porcentaje minimo de coincidencia, default 75
 */
static uint8_t reverse_bits8(uint8_t value) {
    value = (uint8_t)(((value & 0xF0u) >> 4) | ((value & 0x0Fu) << 4));
    value = (uint8_t)(((value & 0xCCu) >> 2) | ((value & 0x33u) << 2));
    value = (uint8_t)(((value & 0xAAu) >> 1) | ((value & 0x55u) << 1));
    return value;
}

static int get_env_int_range(const char *name, int default_value, int min_value, int max_value) {
    const char *env_value = getenv(name);
    char *end_ptr = NULL;
    long parsed;

    if (env_value == NULL || env_value[0] == '\0') {
        return default_value;
    }

    parsed = strtol(env_value, &end_ptr, 10);
    if (end_ptr == env_value || *end_ptr != '\0' || parsed < min_value || parsed > max_value) {
        return default_value;
    }

    return (int)parsed;
}

static unsigned int bitcount8(uint8_t value) {
    unsigned int count = 0;
    while (value != 0u) {
        count += (unsigned int)(value & 1u);
        value >>= 1;
    }
    return count;
}

static int score_mask_pct(const uint8_t user[8], const uint8_t target[8]) {
    unsigned int intersection = 0;
    unsigned int union_count = 0;
    int y;

    for (y = 0; y < 8; y++) {
        intersection += bitcount8((uint8_t)(user[y] & target[y]));
        union_count += bitcount8((uint8_t)(user[y] | target[y]));
    }

    if (union_count == 0u) {
        return 100;
    }

    return (int)((intersection * 100u) / union_count);
}

static void sound_success(void) {
    int i;

    for (i = 0; i < 3; i++) {
        geoboard_buzzer(1);
        usleep(300000u);
        geoboard_buzzer(0);
        usleep(250000u);
    }
}

static void sound_error(void) {
    geoboard_buzzer(1);
    sleep(1u);
    geoboard_buzzer(0);
}

static void blink_mask(const uint8_t mask[8], int times) {
    int i;

    for (i = 0; i < times; i++) {
        geoboard_draw_matrix((uint8_t *)mask);
        usleep(200000u);
        geoboard_clear();
        usleep(200000u);
    }
}

static void run_hardware_game(const uint8_t final_mask[8]) {
    uint8_t target_hw[8];
    uint8_t user_mask[8];
    int y;
    int seconds;
    int tolerance;
    int cursor_x = 0;
    int cursor_y = 0;
    int previous_button = GEO_BTN_NONE;
    int tick = 0;
    const char *skip_hw = getenv("GEOBOARD_SKIP_HARDWARE");
    const char *skip_game = getenv("GEOBOARD_SKIP_GAME");

    if (skip_hw != NULL && strcmp(skip_hw, "1") == 0) {
        printf("[SERVIDOR] GEOBOARD_SKIP_HARDWARE=1, no se envia la mascara al hardware.\n");
        return;
    }

    for (y = 0; y < 8; y++) {
        target_hw[y] = reverse_bits8(final_mask[y]);
        user_mask[y] = 0u;
    }

    seconds = get_env_int_range("GEOBOARD_HW_SECONDS", 10, 0, 120);
    tolerance = get_env_int_range("GEOBOARD_TOLERANCE", 75, 0, 100);

    printf("[SERVIDOR] Iniciando salida fisica mediante libgeoboard.a...\n");

    if (geoboard_init() != 0) {
        fprintf(stderr,
                "[SERVIDOR] AVISO: no se pudo abrir /dev/geoboard. "
                "Se omite salida fisica, pero el procesamiento MPI termino.\n");
        return;
    }

    geoboard_clear();
    geoboard_reset_servo();

    printf("[JUEGO] Mostrando figura objetivo por %d s.\n", seconds);
    if (geoboard_draw_matrix(target_hw) != 0) {
        fprintf(stderr, "[SERVIDOR] AVISO: fallo geoboard_draw_matrix().\n");
        geoboard_close();
        return;
    }

    if (seconds > 0) {
        sleep((unsigned int)seconds);
    }

    geoboard_clear();

    if (skip_game != NULL && strcmp(skip_game, "1") == 0) {
        printf("[JUEGO] GEOBOARD_SKIP_GAME=1, se omitio la etapa interactiva.\n");
        geoboard_close();
        return;
    }

    printf("[JUEGO] Replique el contorno con los botones.\n");
    printf("[JUEGO] UP/DOWN/LEFT/RIGHT mueve cursor, SELECT marca/quita, CHECK valida.\n");
    printf("[JUEGO] Tolerancia de validacion: %d%%.\n", tolerance);

    for (;;) {
        int button = geoboard_read_button();
        uint8_t frame[8];

        if (button != previous_button && button != GEO_BTN_NONE) {
            switch (button) {
                case GEO_BTN_UP:
                    if (cursor_y > 0) cursor_y--;
                    break;
                case GEO_BTN_DOWN:
                    if (cursor_y < 7) cursor_y++;
                    break;
                case GEO_BTN_LEFT:
                    if (cursor_x > 0) cursor_x--;
                    break;
                case GEO_BTN_RIGHT:
                    if (cursor_x < 7) cursor_x++;
                    break;
                case GEO_BTN_SELECT:
                    user_mask[cursor_y] ^= (uint8_t)(1u << cursor_x);
                    break;
                case GEO_BTN_CHECK: {
                    int score = score_mask_pct(user_mask, target_hw);
                    printf("[JUEGO] Coincidencia usuario/objetivo: %d%%.\n", score);

                    if (score >= tolerance) {
                        printf("[JUEGO] Correcto.\n");
                        geoboard_servo(1);
                        sound_success();
                        blink_mask(target_hw, 5);
                        geoboard_servo(0);
                    } else {
                        printf("[JUEGO] Incorrecto. Se muestra el contorno correcto.\n");
                        sound_error();
                        blink_mask(target_hw, 3);
                    }

                    geoboard_clear();
                    geoboard_close();
                    return;
                }
                default:
                    break;
            }
        }

        previous_button = button;

        memcpy(frame, user_mask, sizeof(frame));
        if ((tick / 8) % 2) {
            frame[cursor_y] ^= (uint8_t)(1u << cursor_x);
        }

        geoboard_draw_matrix(frame);
        tick++;
        usleep(30000u);
    }
}

static void thin_mask_to_outer_contour(uint8_t mask[8]) {
    uint8_t thinned[8];
    int row;
    int col;

    memset(thinned, 0, sizeof(thinned));

    /*
     * Si el contorno original viene grueso por el grosor del trazo de la
     * imagen de entrada, se reduce a un contorno 8x8 de un pixel de ancho.
     * Se conservan los extremos izquierdo/derecho de cada fila y los extremos
     * superior/inferior de cada columna. Esto evita que un cuadrado aparezca
     * relleno o con doble grosor en la matriz final.
     */
    for (row = 0; row < 8; row++) {
        int left = -1;
        int right = -1;

        for (col = 0; col < 8; col++) {
            if ((mask[row] & (uint8_t)(1u << (7 - col))) != 0u) {
                if (left < 0) left = col;
                right = col;
            }
        }

        if (left >= 0) {
            thinned[row] |= (uint8_t)(1u << (7 - left));
            thinned[row] |= (uint8_t)(1u << (7 - right));
        }
    }

    for (col = 0; col < 8; col++) {
        int top = -1;
        int bottom = -1;

        for (row = 0; row < 8; row++) {
            if ((mask[row] & (uint8_t)(1u << (7 - col))) != 0u) {
                if (top < 0) top = row;
                bottom = row;
            }
        }

        if (top >= 0) {
            thinned[top] |= (uint8_t)(1u << (7 - col));
            thinned[bottom] |= (uint8_t)(1u << (7 - col));
        }
    }

    memcpy(mask, thinned, sizeof(thinned));
}

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
    uint64_t file_size = 0;
    uint32_t counter = 0;
    uint8_t nonce[CHACHA20_NONCE_SIZE];

    char filename[256];
    char encrypted_path[512];
    char mask_path[512];

    PgmMetadata metadata;
    uint8_t final_mask[8];

    uint64_t total_active = 0;
    uint64_t total_edges = 0;

    int32_t global_min_x = -1;
    int32_t global_min_y = -1;
    int32_t global_max_x = -1;
    int32_t global_max_y = -1;

    int worker_index;
    uint32_t worker_count = 0;

    double t_server_start;
    double t_receive_done;
    double t_header_done;
    double t_distributed_start;
    double t_distributed_done;
    double t_server_end;

    memset(filename, 0, sizeof(filename));
    memset(nonce, 0, sizeof(nonce));
    memset(&metadata, 0, sizeof(metadata));
    memset(final_mask, 0, sizeof(final_mask));

    t_server_start = MPI_Wtime();

    if (world_size < 4) {
        fprintf(stderr,
                "[SERVIDOR] Se necesitan al menos 4 ranks globales: cliente, servidor y 2 workers.\n");
        fprintf(stderr,
                "[SERVIDOR] Modo normal: cliente + servidor + 3 workers = 5 ranks.\n");
        fprintf(stderr,
                "[SERVIDOR] Modo failover: cliente + servidor + 2 workers = 4 ranks.\n");
        return EXIT_FAILURE;
    }

    worker_count = (uint32_t)(world_size - GEOBOARD_FIRST_WORKER_RANK);

    if (worker_count < GEOBOARD_MIN_FAILOVER_WORKERS) {
        fprintf(stderr, "[SERVIDOR] No hay suficientes workers activos.\n");
        return EXIT_FAILURE;
    }

    if (worker_count < GEOBOARD_DEFAULT_WORKER_COUNT) {
        printf("[SERVIDOR] MODO FAILOVER: se ejecuta con %u workers sobrevivientes.\n", worker_count);
        printf("[SERVIDOR] La carga se reagrupara entre los workers activos.\n");
    } else {
        printf("[SERVIDOR] MODO NORMAL: se ejecuta con %u workers.\n", worker_count);
    }

    if (ensure_output_dir() != 0) {
        send_shutdown_to_workers(worker_count);
        return EXIT_FAILURE;
    }

    if (receive_client_file(&encrypted_data,
                            &file_size,
                            filename,
                            sizeof(filename),
                            &counter,
                            nonce) != 0) {
        send_shutdown_to_workers(worker_count);
        return EXIT_FAILURE;
    }

    t_receive_done = MPI_Wtime();

    snprintf(encrypted_path,
             sizeof(encrypted_path),
             "%s/encrypted_%s.bin",
             OUTPUT_DIR,
             filename);

    snprintf(mask_path,
             sizeof(mask_path),
             "%s/mask8x8_%s.txt",
             OUTPUT_DIR,
             filename);

    write_file(encrypted_path, encrypted_data, file_size);
    printf("[SERVIDOR] Archivo cifrado guardado en: %s\n", encrypted_path);

    /*
     * El servidor descifra solo el header PGM, no todos los pixeles.
     */
    if (parse_encrypted_pgm_metadata(encrypted_data,
                                     file_size,
                                     counter,
                                     nonce,
                                     &metadata) != 0) {
        fprintf(stderr,
                "[SERVIDOR] No se pudo interpretar metadata PGM cifrada. Use PGM P5.\n");
        free(encrypted_data);
        send_shutdown_to_workers(worker_count);
        return EXIT_FAILURE;
    }

    t_header_done = MPI_Wtime();

    printf("[SERVIDOR] Metadata PGM leida desde header descifrado: %ux%u, max=%u, pixel_offset=%llu\n",
           metadata.width,
           metadata.height,
           metadata.max_value,
           (unsigned long long)metadata.pixel_data_offset);
    printf("[SERVIDOR] El servidor NO descifra los pixeles completos; los workers descifran sus franjas.\n");
    printf("[SERVIDOR] La imagen puede tener cualquier resolucion positiva; se divide en malla 3x3 y se redistribuye entre los workers activos.\n");

    t_distributed_start = MPI_Wtime();

    for (worker_index = 0; worker_index < (int)worker_count; worker_index++) {
        int worker_rank = GEOBOARD_FIRST_WORKER_RANK + worker_index;

        if (send_worker_task(worker_rank,
                             encrypted_data,
                             file_size,
                             &metadata,
                             (uint32_t)worker_index,
                             worker_count,
                             counter,
                             nonce) != 0) {
            fprintf(stderr,
                    "[SERVIDOR] Error enviando tarea cifrada al worker rank %d.\n",
                    worker_rank);

            free(encrypted_data);
            send_shutdown_to_workers(worker_count);
            return EXIT_FAILURE;
        }
    }

    for (worker_index = 0; worker_index < (int)worker_count; worker_index++) {
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

        printf("[SERVIDOR] Resultado worker %u: active=%llu, edge_metric=%llu\n",
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

    t_distributed_done = MPI_Wtime();

    thin_mask_to_outer_contour(final_mask);
    printf("[SERVIDOR] Mascara reducida a contorno externo 8x8 de un pixel.\n");

    print_mask(final_mask);
    save_mask_file(mask_path, final_mask);

    printf("[SERVIDOR] Mascara final guardada en: %s\n", mask_path);
    printf("[SERVIDOR] Pixeles activos globales: %llu\n",
           (unsigned long long)total_active);
    printf("[SERVIDOR] Bordes/metrica aproximada global: %llu\n",
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

    t_server_end = MPI_Wtime();

    printf("[SERVIDOR] Tiempos internos:\n");
    printf("[SERVIDOR] - Recepcion cliente: %.3f s\n",
           t_receive_done - t_server_start);
    printf("[SERVIDOR] - Lectura header PGM cifrado: %.3f s\n",
           t_header_done - t_receive_done);
    printf("[SERVIDOR] - Distribucion cifrada + procesamiento workers: %.3f s\n",
           t_distributed_done - t_distributed_start);
    printf("[SERVIDOR] - Total servidor: %.3f s\n",
           t_server_end - t_server_start);

    run_hardware_game(final_mask);

    printf("[SERVIDOR] Procesamiento distribuido terminado.\n");
    printf("[SERVIDOR] Flujo completo: cliente -> servidor MPI -> workers MPI locales -> libgeoboard.a -> driver GPIO.\n");

    free(encrypted_data);
    return EXIT_SUCCESS;
}
