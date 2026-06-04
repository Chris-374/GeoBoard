/*
 * pgm_image.c
 *
 * Parser simple para PGM P2/P5.
 * Se convierte la imagen a un arreglo lineal de pixeles normalizados 0..255.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pgm_image.h"

static int pgm_next_token(const uint8_t *data,
                          uint64_t size,
                          uint64_t *pos,
                          char *token,
                          size_t token_size) {
    size_t len = 0;

    if (token_size == 0) {
        return -1;
    }

    /*
     * Ignora espacios y comentarios.
     * En PGM los comentarios inician con '#'.
     */
    while (*pos < size) {
        uint8_t c = data[*pos];

        if (isspace((unsigned char)c)) {
            (*pos)++;
            continue;
        }

        if (c == '#') {
            while (*pos < size && data[*pos] != '\n') {
                (*pos)++;
            }
            continue;
        }

        break;
    }

    if (*pos >= size) {
        return -1;
    }

    while (*pos < size) {
        uint8_t c = data[*pos];

        if (isspace((unsigned char)c) || c == '#') {
            break;
        }

        if (len + 1 < token_size) {
            token[len++] = (char)c;
        }

        (*pos)++;
    }

    token[len] = '\0';
    return (len > 0) ? 0 : -1;
}

static int pgm_skip_whitespace_and_comments(const uint8_t *data,
                                            uint64_t size,
                                            uint64_t *pos) {
    while (*pos < size) {
        uint8_t c = data[*pos];

        if (isspace((unsigned char)c)) {
            (*pos)++;
            continue;
        }

        if (c == '#') {
            while (*pos < size && data[*pos] != '\n') {
                (*pos)++;
            }
            continue;
        }

        break;
    }

    return (*pos < size) ? 0 : -1;
}

static uint8_t normalize_sample(uint32_t sample,
                                uint32_t max_value) {
    uint32_t normalized;

    if (max_value == 0) {
        return 0;
    }

    if (sample > max_value) {
        sample = max_value;
    }

    normalized = (sample * 255u + (max_value / 2u)) / max_value;

    if (normalized > 255u) {
        normalized = 255u;
    }

    return (uint8_t)normalized;
}

int parse_pgm_image(const uint8_t *data,
                    uint64_t size,
                    PgmImage *image) {
    char token[64];
    uint64_t pos = 0;
    uint64_t total_pixels;
    uint64_t i;
    int is_ascii = 0;

    if (data == NULL || image == NULL || size == 0) {
        return -1;
    }

    memset(image, 0, sizeof(*image));

    if (pgm_next_token(data, size, &pos, token, sizeof(token)) != 0) {
        fprintf(stderr, "[SERVIDOR] No se pudo leer magic del PGM.\n");
        return -1;
    }

    if (strcmp(token, "P2") == 0) {
        is_ascii = 1;
    } else if (strcmp(token, "P5") == 0) {
        is_ascii = 0;
    } else {
        fprintf(stderr,
                "[SERVIDOR] Formato no soportado. Use PGM P2 o P5. Magic recibido: %s\n",
                token);
        return -1;
    }

    if (pgm_next_token(data, size, &pos, token, sizeof(token)) != 0) {
        fprintf(stderr, "[SERVIDOR] No se pudo leer ancho del PGM.\n");
        return -1;
    }
    image->width = (uint32_t)strtoul(token, NULL, 10);

    if (pgm_next_token(data, size, &pos, token, sizeof(token)) != 0) {
        fprintf(stderr, "[SERVIDOR] No se pudo leer alto del PGM.\n");
        return -1;
    }
    image->height = (uint32_t)strtoul(token, NULL, 10);

    if (pgm_next_token(data, size, &pos, token, sizeof(token)) != 0) {
        fprintf(stderr, "[SERVIDOR] No se pudo leer max_value del PGM.\n");
        return -1;
    }
    image->max_value = (uint32_t)strtoul(token, NULL, 10);

    if (image->width == 0 || image->height == 0 || image->max_value == 0) {
        fprintf(stderr, "[SERVIDOR] Header PGM invalido.\n");
        return -1;
    }

    if (image->max_value > 255u) {
        fprintf(stderr, "[SERVIDOR] Este prototipo solo soporta PGM con max_value <= 255.\n");
        return -1;
    }

    total_pixels = (uint64_t)image->width * (uint64_t)image->height;

    image->pixels = (uint8_t *)malloc((size_t)total_pixels);
    if (image->pixels == NULL) {
        fprintf(stderr, "[SERVIDOR] No hay memoria para pixeles PGM.\n");
        return -1;
    }

    if (is_ascii) {
        /*
         * P2 almacena cada pixel como numero ASCII.
         */
        for (i = 0; i < total_pixels; i++) {
            uint32_t sample;

            if (pgm_next_token(data, size, &pos, token, sizeof(token)) != 0) {
                fprintf(stderr,
                        "[SERVIDOR] PGM P2 incompleto en pixel %llu.\n",
                        (unsigned long long)i);
                free(image->pixels);
                image->pixels = NULL;
                return -1;
            }

            sample = (uint32_t)strtoul(token, NULL, 10);
            image->pixels[i] = normalize_sample(sample, image->max_value);
        }
    } else {
        /*
         * P5 almacena los pixeles como bytes binarios justo despues del header.
         */
        if (pgm_skip_whitespace_and_comments(data, size, &pos) != 0) {
            fprintf(stderr, "[SERVIDOR] PGM P5 sin datos de pixeles.\n");
            free(image->pixels);
            image->pixels = NULL;
            return -1;
        }

        if (size - pos < total_pixels) {
            fprintf(stderr,
                    "[SERVIDOR] PGM P5 incompleto. Esperados %llu bytes de pixeles.\n",
                    (unsigned long long)total_pixels);
            free(image->pixels);
            image->pixels = NULL;
            return -1;
        }

        for (i = 0; i < total_pixels; i++) {
            image->pixels[i] = normalize_sample((uint32_t)data[pos + i],
                                                image->max_value);
        }
    }

    return 0;
}

void free_pgm_image(PgmImage *image) {
    if (image != NULL) {
        free(image->pixels);
        image->pixels = NULL;
        image->width = 0;
        image->height = 0;
        image->max_value = 0;
    }
}
