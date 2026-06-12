/*
 * geoboard_client_mpi_chacha20.c
 * Cliente inicial para GeoBoard Interactivo.
 *
 * Este programa NO usa sockets. El envio al servidor se hace unicamente
 * con OpenMPI, usando MPI_Send sobre MPI_COMM_WORLD.
 *
 * Distribucion esperada del job MPI:
 *   rank 0 -> cliente, este programa
 *   rank 1 -> servidor/orquestador
 *   rank 2, 3, 4 -> nodos de procesamiento
 *
 * Ejemplo futuro de ejecucion con MPMD:
 *   mpirun -np 1 ./geoboard_client imagen.pgm : -np 1 ./geoboard_server : -np 3 ./geoboard_worker
 *
 *   - ChaCha20 implementado en C.
 *   - La key es compartida entre cliente y servidor para el prototipo.
 *   - El cliente envia al servidor el nonce y el counter usados para el cifrado.
 */

#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define GEOBOARD_CLIENT_RANK 0
#define GEOBOARD_SERVER_RANK 1

#define GEOBOARD_MAGIC   0x47424F44u  /* 'GBOD' */
#define GEOBOARD_VERSION 2u
#define GEOBOARD_CHUNK_SIZE 65536u

#define CHACHA20_KEY_SIZE   32u
#define CHACHA20_NONCE_SIZE 12u
#define CHACHA20_BLOCK_SIZE 64u
#define CHACHA20_DEFAULT_COUNTER 1u

/*
 * Key compartida para el prototipo.
 * Debe existir exactamente igual en el servidor para poder descifrar.
 */
static const uint8_t GEOBOARD_CHACHA20_KEY[CHACHA20_KEY_SIZE] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
};

/*
 * Nonce por defecto para el prototipo.
 * Igual que el counter, se envia al servidor por MPI para que pueda descifrar.
 */
static const uint8_t GEOBOARD_DEFAULT_NONCE[CHACHA20_NONCE_SIZE] = {
    0x47, 0x45, 0x4F, 0x42,
    0x4F, 0x41, 0x52, 0x44,
    0x00, 0x00, 0x00, 0x01
};

/* Tags MPI usados por el protocolo cliente -> servidor. */
enum {
    TAG_MAGIC = 100,
    TAG_VERSION,
    TAG_FILE_SIZE,
    TAG_FILENAME_LEN,
    TAG_COUNTER,
    TAG_CHUNK_SIZE,
    TAG_NONCE,
    TAG_FILENAME,
    TAG_FILE_CHUNK,
    TAG_END_OF_FILE
};

static const char *get_filename_from_path(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return path;
    }
    return slash + 1;
}

static void print_usage(const char *program_name) {
    fprintf(stderr, "Uso:\n");
    fprintf(stderr, "  %s <imagen.pgm|bmp|raw> [counter]\n", program_name);
    fprintf(stderr, "\nEjemplo futuro con OpenMPI:\n");
    fprintf(stderr, "  mpirun -np 1 %s images/cuadrado.pgm : -np 1 ./geoboard_server : -np 3 ./geoboard_worker\n", program_name);
    fprintf(stderr, "\nNotas:\n");
    fprintf(stderr, "  - La key de ChaCha20 esta compartida en el codigo del cliente y servidor.\n");
    fprintf(stderr, "  - El nonce se envia por MPI como metadata.\n");
    fprintf(stderr, "  - El counter es opcional y por defecto vale %u.\n", CHACHA20_DEFAULT_COUNTER);
}

static int read_complete_file(const char *path, uint8_t **out_data, uint64_t *out_size) {
    FILE *file = NULL;
    long file_size_long = 0;
    uint8_t *buffer = NULL;
    size_t bytes_read = 0;

    if (path == NULL || out_data == NULL || out_size == NULL) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "[CLIENTE] No se pudo abrir el archivo '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "[CLIENTE] No se pudo calcular el tamano del archivo.\n");
        fclose(file);
        return -1;
    }

    file_size_long = ftell(file);
    if (file_size_long < 0) {
        fprintf(stderr, "[CLIENTE] ftell fallo al calcular el tamano.\n");
        fclose(file);
        return -1;
    }

    if (file_size_long == 0) {
        fprintf(stderr, "[CLIENTE] El archivo esta vacio.\n");
        fclose(file);
        return -1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[CLIENTE] No se pudo volver al inicio del archivo.\n");
        fclose(file);
        return -1;
    }

    buffer = (uint8_t *)malloc((size_t)file_size_long);
    if (buffer == NULL) {
        fprintf(stderr, "[CLIENTE] No hay memoria suficiente para cargar el archivo.\n");
        fclose(file);
        return -1;
    }

    bytes_read = fread(buffer, 1, (size_t)file_size_long, file);
    fclose(file);

    if (bytes_read != (size_t)file_size_long) {
        fprintf(stderr, "[CLIENTE] Lectura incompleta del archivo.\n");
        free(buffer);
        return -1;
    }

    *out_data = buffer;
    *out_size = (uint64_t)file_size_long;
    return 0;
}

static uint32_t rotl32(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

static uint32_t load32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void store32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

static void chacha20_block(const uint8_t key[CHACHA20_KEY_SIZE],
                           uint32_t counter,
                           const uint8_t nonce[CHACHA20_NONCE_SIZE],
                           uint8_t output[CHACHA20_BLOCK_SIZE]) {
    static const uint32_t constants[4] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
    };

    uint32_t state[16];
    uint32_t working[16];
    int i;

    state[0] = constants[0];
    state[1] = constants[1];
    state[2] = constants[2];
    state[3] = constants[3];

    for (i = 0; i < 8; i++) {
        state[4 + i] = load32_le(key + (i * 4));
    }

    state[12] = counter;
    state[13] = load32_le(nonce + 0);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    for (i = 0; i < 16; i++) {
        working[i] = state[i];
    }

    for (i = 0; i < 10; i++) {
        /* Column rounds */
        quarter_round(&working[0], &working[4], &working[8],  &working[12]);
        quarter_round(&working[1], &working[5], &working[9],  &working[13]);
        quarter_round(&working[2], &working[6], &working[10], &working[14]);
        quarter_round(&working[3], &working[7], &working[11], &working[15]);

        /* Diagonal rounds */
        quarter_round(&working[0], &working[5], &working[10], &working[15]);
        quarter_round(&working[1], &working[6], &working[11], &working[12]);
        quarter_round(&working[2], &working[7], &working[8],  &working[13]);
        quarter_round(&working[3], &working[4], &working[9],  &working[14]);
    }

    for (i = 0; i < 16; i++) {
        working[i] += state[i];
        store32_le(output + (i * 4), working[i]);
    }
}

static void chacha20_encrypt_bytes(uint8_t *data,
                                   uint64_t size,
                                   const uint8_t key[CHACHA20_KEY_SIZE],
                                   const uint8_t nonce[CHACHA20_NONCE_SIZE],
                                   uint32_t initial_counter) {
    uint8_t keystream[CHACHA20_BLOCK_SIZE];
    uint64_t offset = 0;
    uint32_t counter = initial_counter;

    while (offset < size) {
        uint32_t i;
        uint64_t remaining = size - offset;
        uint32_t block_bytes = (remaining > CHACHA20_BLOCK_SIZE)
            ? CHACHA20_BLOCK_SIZE
            : (uint32_t)remaining;

        chacha20_block(key, counter, nonce, keystream);

        for (i = 0; i < block_bytes; i++) {
            data[offset + i] ^= keystream[i];
        }

        offset += block_bytes;
        counter++;
    }
}

static int send_metadata_to_server(uint64_t file_size,
                                   uint32_t filename_len,
                                   uint32_t counter,
                                   const uint8_t nonce[CHACHA20_NONCE_SIZE]) {
    const uint32_t magic = GEOBOARD_MAGIC;
    const uint32_t version = GEOBOARD_VERSION;
    const uint32_t chunk_size = GEOBOARD_CHUNK_SIZE;

    if (MPI_Send(&magic, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_MAGIC, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send(&version, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_VERSION, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send(&file_size, 1, MPI_UINT64_T, GEOBOARD_SERVER_RANK, TAG_FILE_SIZE, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send(&filename_len, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_FILENAME_LEN, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send(&counter, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_COUNTER, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send(&chunk_size, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_CHUNK_SIZE, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send((void *)nonce, CHACHA20_NONCE_SIZE, MPI_BYTE, GEOBOARD_SERVER_RANK, TAG_NONCE, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    return 0;
}

static int send_encrypted_file_to_server(const char *filename, const uint8_t *data, uint64_t size) {
    uint64_t offset = 0;
    uint32_t filename_len = 0;
    uint32_t end_marker = 1u;

    if (filename == NULL || data == NULL || size == 0) {
        return -1;
    }

    filename_len = (uint32_t)strlen(filename);
    if (filename_len == 0) {
        fprintf(stderr, "[CLIENTE] Nombre de archivo invalido.\n");
        return -1;
    }

    if (MPI_Send(filename, (int)filename_len, MPI_CHAR, GEOBOARD_SERVER_RANK, TAG_FILENAME, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    while (offset < size) {
        uint64_t remaining = size - offset;
        int current_chunk = (remaining > GEOBOARD_CHUNK_SIZE) ? (int)GEOBOARD_CHUNK_SIZE : (int)remaining;

        if (MPI_Send(data + offset, current_chunk, MPI_BYTE, GEOBOARD_SERVER_RANK, TAG_FILE_CHUNK, MPI_COMM_WORLD) != MPI_SUCCESS) {
            return -1;
        }

        offset += (uint64_t)current_chunk;
    }

    if (MPI_Send(&end_marker, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_END_OF_FILE, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    int rank = -1;
    int world_size = 0;
    const char *image_path = NULL;
    const char *filename = NULL;
    uint8_t *image_data = NULL;
    uint64_t image_size = 0;
    uint32_t counter = CHACHA20_DEFAULT_COUNTER;
    uint32_t filename_len = 0;
    uint8_t nonce[CHACHA20_NONCE_SIZE];

    memcpy(nonce, GEOBOARD_DEFAULT_NONCE, CHACHA20_NONCE_SIZE);

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (rank != GEOBOARD_CLIENT_RANK) {
        fprintf(stderr, "[CLIENTE] Este binario solo debe ejecutarse como rank 0. Rank actual: %d\n", rank);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (world_size < 2) {
        fprintf(stderr, "[CLIENTE] Error: se necesita al menos rank 0 cliente y rank 1 servidor.\n");
        fprintf(stderr, "[CLIENTE] No use sockets; el servidor debe correr dentro del mismo job MPI.\n");
        print_usage(argv[0]);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (argc < 2) {
        print_usage(argv[0]);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    image_path = argv[1];
    filename = get_filename_from_path(image_path);
    filename_len = (uint32_t)strlen(filename);

    if (argc >= 3) {
        char *end_ptr = NULL;
        unsigned long parsed_counter = strtoul(argv[2], &end_ptr, 0);
        if (end_ptr == argv[2] || *end_ptr != '\0') {
            fprintf(stderr, "[CLIENTE] Counter invalido. Use decimal o hexadecimal, por ejemplo 1 o 0x1.\n");
            MPI_Finalize();
            return EXIT_FAILURE;
        }
        counter = (uint32_t)parsed_counter;
    }

    if (filename_len == 0) {
        fprintf(stderr, "[CLIENTE] Nombre de archivo invalido.\n");
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    printf("[CLIENTE] Leyendo imagen: %s\n", image_path);
    if (read_complete_file(image_path, &image_data, &image_size) != 0) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    printf("[CLIENTE] Imagen cargada: %llu bytes\n", (unsigned long long)image_size);
    printf("[CLIENTE] Cifrando contenido con ChaCha20...\n");
    chacha20_encrypt_bytes(image_data, image_size, GEOBOARD_CHACHA20_KEY, nonce, counter);

    printf("[CLIENTE] Enviando metadata al servidor MPI rank %d...\n", GEOBOARD_SERVER_RANK);
    if (send_metadata_to_server(image_size, filename_len, counter, nonce) != 0) {
        fprintf(stderr, "[CLIENTE] Error enviando metadata al servidor.\n");
        free(image_data);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    printf("[CLIENTE] Enviando archivo cifrado al servidor MPI rank %d...\n", GEOBOARD_SERVER_RANK);
    if (send_encrypted_file_to_server(filename, image_data, image_size) != 0) {
        fprintf(stderr, "[CLIENTE] Error enviando archivo cifrado al servidor.\n");
        free(image_data);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    printf("[CLIENTE] Archivo enviado correctamente por OpenMPI.\n");
    printf("[CLIENTE] Nombre enviado: %s\n", filename);
    printf("[CLIENTE] Counter enviado: %u\n", counter);
    printf("[CLIENTE] Nonce enviado: ");
    for (int i = 0; i < (int)CHACHA20_NONCE_SIZE; i++) {
        printf("%02X", nonce[i]);
        if (i + 1 < (int)CHACHA20_NONCE_SIZE) {
            printf(":");
        }
    }
    printf("\n");

    free(image_data);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
