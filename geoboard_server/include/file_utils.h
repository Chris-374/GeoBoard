#ifndef FILE_UTILS_H
#define FILE_UTILS_H

/*
 * file_utils.h
 *
 * Funciones pequeñas para crear carpetas, guardar archivos y evitar nombres
 * de archivo peligrosos o incomodos.
 */

#include <stdint.h>
#include <stddef.h>

int ensure_output_dir(void);

void sanitize_filename(const char *input,
                       char *output,
                       size_t output_size);

int write_file(const char *path,
               const uint8_t *data,
               uint64_t size);

int save_mask_file(const char *path,
                   const uint8_t mask[8]);

void print_mask(const uint8_t mask[8]);

#endif
