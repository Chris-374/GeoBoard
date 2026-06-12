/*
 * file_utils.c
 *
 * Utilidades de archivo para el servidor.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "file_utils.h"
#include "geoboard_protocol.h"

int ensure_output_dir(void) {
    if (mkdir(OUTPUT_DIR, 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "[SERVIDOR] No se pudo crear carpeta '%s': %s\n",
                OUTPUT_DIR, strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Limpia el nombre recibido del cliente.
 * Esto evita guardar archivos con rutas raras o caracteres problematicos.
 */
void sanitize_filename(const char *input,
                       char *output,
                       size_t output_size) {
    size_t i;
    size_t j = 0;

    if (output_size == 0) {
        return;
    }

    for (i = 0; input[i] != '\0' && j + 1 < output_size; i++) {
        char c = input[i];

        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') {
            output[j++] = c;
        } else {
            output[j++] = '_';
        }
    }

    output[j] = '\0';

    if (j == 0) {
        strncpy(output, "imagen_recibida.pgm", output_size - 1);
        output[output_size - 1] = '\0';
    }
}

int write_file(const char *path,
               const uint8_t *data,
               uint64_t size) {
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        fprintf(stderr, "[SERVIDOR] No se pudo escribir '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    if (fwrite(data, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "[SERVIDOR] Escritura incompleta en '%s'.\n", path);
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

int save_mask_file(const char *path,
                   const uint8_t mask[8]) {
    FILE *file = fopen(path, "w");
    int y;
    int x;

    if (file == NULL) {
        fprintf(stderr, "[SERVIDOR] No se pudo escribir mascara '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int bit = (mask[y] >> (7 - x)) & 1;
            fputc(bit ? '1' : '0', file);
        }
        fputc('\n', file);
    }

    fclose(file);
    return 0;
}

void print_mask(const uint8_t mask[8]) {
    int y;
    int x;

    printf("[SERVIDOR] Mascara final 8x8:\n");

    for (y = 0; y < 8; y++) {
        printf("[SERVIDOR] ");
        for (x = 0; x < 8; x++) {
            int bit = (mask[y] >> (7 - x)) & 1;
            printf("%c", bit ? '1' : '0');
        }
        printf("\n");
    }
}
