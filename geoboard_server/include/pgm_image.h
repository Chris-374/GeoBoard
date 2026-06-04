#ifndef PGM_IMAGE_H
#define PGM_IMAGE_H

/*
 * pgm_image.h
 *
 * Parser simple para imagenes PGM.
 *
 * Se soportan:
 * - P2: PGM ASCII
 * - P5: PGM binario
 *
 * Para este proyecto se recomienda PGM porque es simple de procesar en C
 * sin librerias externas.
 */

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t max_value;
    uint8_t *pixels;
} PgmImage;

int parse_pgm_image(const uint8_t *data,
                    uint64_t size,
                    PgmImage *image);

void free_pgm_image(PgmImage *image);

#endif
