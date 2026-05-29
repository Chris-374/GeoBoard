/*
 * geoboard_client_mpi.c
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
#define GEOBOARD_VERSION 1u

#define GEOBOARD_DEFAULT_SEED 0x1234ABCDu
#define GEOBOARD_CHUNK_SIZE   65536u

/* Tags MPI usados por el protocolo cliente -> servidor. */
enum {
    TAG_MAGIC = 100,
    TAG_VERSION,
    TAG_FILE_SIZE,
    TAG_FILENAME_LEN,
    TAG_KEY_SEED,
    TAG_CHUNK_SIZE,
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
    fprintf(stderr, "  %s <imagen.pgm|bmp|raw> [semilla_cifrado]\n", program_name);
    fprintf(stderr, "\nEjemplo futuro con OpenMPI:\n");
    fprintf(stderr, "  mpirun -np 1 %s images/cuadrado.pgm : -np 1 ./geoboard_server : -np 3 ./geoboard_worker\n", program_name);
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

/*
 * Cifrado inicial simple por flujo XOR.
 * Es simetrico: aplicar esta misma funcion otra vez con la misma semilla descifra.
 * Para el prototipo inicial sirve para cumplir el flujo de informacion cifrada.
 */
static uint8_t next_key_byte(uint32_t *state) {
    *state = (*state * 1664525u) + 1013904223u;
    return (uint8_t)((*state >> 24) & 0xFFu);
}

static void encrypt_bytes(uint8_t *data, uint64_t size, uint32_t seed) {
    uint64_t i = 0;
    uint32_t state = seed;

    for (i = 0; i < size; i++) {
        data[i] = (uint8_t)(data[i] ^ next_key_byte(&state));
    }
}

static int send_metadata_to_server(uint64_t file_size, uint32_t filename_len, uint32_t seed) {
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

    if (MPI_Send(&seed, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_KEY_SEED, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return -1;
    }

    if (MPI_Send(&chunk_size, 1, MPI_UINT32_T, GEOBOARD_SERVER_RANK, TAG_CHUNK_SIZE, MPI_COMM_WORLD) != MPI_SUCCESS) {
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
    uint32_t seed = GEOBOARD_DEFAULT_SEED;
    uint32_t filename_len = 0;

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
        unsigned long parsed_seed = strtoul(argv[2], &end_ptr, 0);
        if (end_ptr == argv[2] || *end_ptr != '\0') {
            fprintf(stderr, "[CLIENTE] Semilla invalida. Use decimal o hexadecimal, por ejemplo 0x1234ABCD.\n");
            MPI_Finalize();
            return EXIT_FAILURE;
        }
        seed = (uint32_t)parsed_seed;
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
    printf("[CLIENTE] Cifrando contenido byte por byte...\n");
    encrypt_bytes(image_data, image_size, seed);

    printf("[CLIENTE] Enviando metadata al servidor MPI rank %d...\n", GEOBOARD_SERVER_RANK);
    if (send_metadata_to_server(image_size, filename_len, seed) != 0) {
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
    printf("[CLIENTE] Semilla enviada: 0x%08X\n", seed);

    free(image_data);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
