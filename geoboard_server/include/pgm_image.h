#ifndef PGM_IMAGE_H
#define PGM_IMAGE_H

/*
 * pgm_image.h
 *
 * Parser simple para imagenes PGM.
 *
 * Se soportan:
 * - P2: PGM ASCII para el parser completo antiguo.
 * - P5: PGM binario para el flujo nuevo con workers descifrando franjas.
 *
 * Para el flujo cifrado correcto del proyecto se recomienda P5, porque los
 * pixeles son bytes contiguos y se pueden dividir por franjas sin descifrar
 * toda la imagen en el servidor.
 */

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t max_value;
    uint8_t *pixels;
} PgmImage;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t max_value;
    uint64_t pixel_data_offset;
    uint64_t pixel_data_size;
    int is_binary_p5;
} PgmMetadata;

int parse_pgm_metadata(const uint8_t *data,
                       uint64_t size,
                       PgmMetadata *metadata);

int parse_pgm_image(const uint8_t *data,
                    uint64_t size,
                    PgmImage *image);

void free_pgm_image(PgmImage *image);

#endif
