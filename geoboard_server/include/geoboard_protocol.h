#ifndef GEOBOARD_PROTOCOL_H
#define GEOBOARD_PROTOCOL_H

/*
 * geoboard_protocol.h
 *
 * Constantes y estructuras compartidas entre el servidor/orquestador
 * y los workers del proyecto GeoBoard.
 *
 * Importante:
 * - Este proyecto usa OpenMPI.
 * - No se usan sockets.
 * - La comunicacion se realiza con MPI_Send/MPI_Recv.
 * - El rank 0 pertenece al cliente.
 * - El rank 1 pertenece al servidor.
 * - Los ranks 2 en adelante pertenecen a workers.
 * - En modo normal se usan 3 workers.
 * - En modo failover se puede relanzar con 2 workers si un nodo cae.
 */

#include <stdint.h>

#define GEOBOARD_CLIENT_RANK 0
#define GEOBOARD_SERVER_RANK 1

#define GEOBOARD_FIRST_WORKER_RANK 2
#define GEOBOARD_DEFAULT_WORKER_COUNT 3
#define GEOBOARD_MIN_FAILOVER_WORKERS 2
#define GEOBOARD_MAX_REGIONS_PER_WORKER 9

/* Magic usado para validar que el mensaje recibido viene del protocolo GeoBoard. */
#define GEOBOARD_MAGIC   0x47424F44u  /* 'GBOD' */
#define GEOBOARD_VERSION 2u

/* El cliente y el servidor envian la imagen por bloques de este tamano. */
#define GEOBOARD_CHUNK_SIZE 65536u

/* Parametros de ChaCha20. */
#define CHACHA20_KEY_SIZE   32u
#define CHACHA20_NONCE_SIZE 12u
#define CHACHA20_BLOCK_SIZE 64u

/*
 * Magics internos del servidor hacia los workers.
 * Sirven para detectar si el worker recibio una tarea valida, un resultado
 * valido o una orden de apagado.
 */
#define WORKER_TASK_MAGIC     0x4754424Bu  /* 'GTBK' */
#define WORKER_RESULT_MAGIC   0x47525253u  /* 'GRRS' */
#define WORKER_SHUTDOWN_MAGIC 0x47535450u  /* 'GSTP' */

/* Carpeta donde el servidor guarda los archivos recibidos/procesados. */
#define OUTPUT_DIR "server_output"

/*
 * Umbral de binarizacion.
 * En PGM, valores bajos representan pixeles oscuros.
 * Para el proyecto asumimos figura oscura sobre fondo claro.
 */
#define PIXEL_THRESHOLD 128u

/*
 * Tags que deben coincidir con los usados por el cliente
 * geoboard_client_mpi_chacha20.c.
 */
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

/* Tags internos para mensajes servidor -> worker y worker -> servidor. */
enum {
    TAG_WORKER_HEADER = 300,
    TAG_WORKER_PAYLOAD,
    TAG_WORKER_RESULT
};

/*
 * RegionInfo describe una region rectangular de la imagen original.
 * La propuesta divide la imagen en una malla 3x3:
 *
 *   [1][2][3]
 *   [4][5][6]
 *   [7][8][9]
 *
 * En modo normal cada worker recibe una fila completa, o sea 3 regiones.
 * En modo failover el servidor puede reagrupar regiones y mandar mas de
 * 3 regiones a un worker sobreviviente.
 */
typedef struct {
    uint32_t region_id;
    uint32_t start_x;
    uint32_t start_y;
    uint32_t width;
    uint32_t height;
} RegionInfo;

/*
 * Header que el servidor envia a cada worker.
 *
 * Cambio importante:
 * - El payload que viaja servidor -> worker ahora es una franja CIFRADA
 *   del archivo original recibido desde el cliente.
 * - El servidor NO descifra todos los pixeles para armar regiones.
 * - El worker usa payload_file_offset para descifrar su franja en la posicion
 *   correcta del flujo ChaCha20 original.
 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t worker_index;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t region_count;
    uint32_t payload_size;
    uint32_t counter;
    uint8_t nonce[CHACHA20_NONCE_SIZE];

    /* Offset absoluto del payload dentro del archivo cifrado original. */
    uint64_t payload_file_offset;

    /* El payload cifrado corresponde a una franja completa de filas. */
    uint32_t stripe_start_y;
    uint32_t stripe_height;

    RegionInfo regions[GEOBOARD_MAX_REGIONS_PER_WORKER];
} WorkerTaskHeader;

/*
 * Resultado parcial que cada worker devuelve al servidor.
 * Incluye una mascara parcial 8x8 y metricas locales.
 */
typedef struct {
    uint32_t magic;
    uint32_t worker_index;
    uint32_t has_content;
    uint8_t mask[8];
    uint64_t active_pixels;
    uint64_t edge_pixels;
    int32_t bbox_min_x;
    int32_t bbox_min_y;
    int32_t bbox_max_x;
    int32_t bbox_max_y;
} WorkerResult;

/*
 * Key compartida para el prototipo.
 * Debe ser exactamente igual que la key usada en el cliente.
 *
 * En un sistema real no deberia estar quemada en codigo fuente, pero para
 * este proyecto facilita demostrar cifrado simetrico sin agregar dependencias.
 */
extern const uint8_t GEOBOARD_CHACHA20_KEY[CHACHA20_KEY_SIZE];

#endif
