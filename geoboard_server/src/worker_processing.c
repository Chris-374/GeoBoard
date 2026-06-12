/*
 * worker_processing.c
 *
 * Procesamiento local de cada worker.
 *
 * Esta version agrega una carga computacional real para justificar mejor
 * el procesamiento distribuido:
 *
 * - binarizacion por umbral
 * - conteo de pixeles activos
 * - bounding box local
 * - reduccion a mascara parcial 8x8
 * - varias pasadas pesadas de filtrado 3x3 + deteccion Sobel aproximada
 *
 * La cantidad de pasadas pesadas se controla con la variable de entorno:
 *
 *   export GEOBOARD_HEAVY_PASSES=8
 *
 * Si no se define, se usa DEFAULT_HEAVY_PASSES.
 *
 * Importante:
 * No se usa sleep(). La duracion aumenta porque el worker hace mas trabajo
 * real de procesamiento sobre los pixeles.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "worker_processing.h"

#define DEFAULT_HEAVY_PASSES 4u
#define MAX_HEAVY_PASSES 100u
#define SOBEL_THRESHOLD 90u

int is_active_pixel(uint8_t pixel) {
    return pixel < PIXEL_THRESHOLD;
}

uint32_t geoboard_get_processing_passes(void) {
    const char *env_value = getenv("GEOBOARD_HEAVY_PASSES");
    char *end_ptr = NULL;
    unsigned long parsed;

    if (env_value == NULL || env_value[0] == '\0') {
        return DEFAULT_HEAVY_PASSES;
    }

    parsed = strtoul(env_value, &end_ptr, 10);

    if (end_ptr == env_value || *end_ptr != '\0') {
        return DEFAULT_HEAVY_PASSES;
    }

    if (parsed > MAX_HEAVY_PASSES) {
        parsed = MAX_HEAVY_PASSES;
    }

    return (uint32_t)parsed;
}

/*
 * Valor absoluto entero pequeño, para evitar depender de math.h.
 */
static uint32_t abs_i32(int32_t value) {
    return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

/*
 * Carga pesada real:
 *
 * Para cada region se ejecutan varias pasadas. En cada pasada:
 * 1. Filtro de suavizado 3x3.
 * 2. Deteccion de bordes tipo Sobel sobre la imagen suavizada.
 * 3. Se acumula un conteo de bordes.
 *
 * Esto hace que una imagen grande tenga trabajo proporcional a:
 *
 *   ancho * alto * cantidad_de_pasadas
 *
 * Por eso se justifica distribuir la imagen entre varios workers.
 */
static uint64_t run_heavy_filter_pipeline(const uint8_t *region_pixels,
                                          uint32_t width,
                                          uint32_t height,
                                          uint32_t passes) {
    uint64_t total_pixels = (uint64_t)width * (uint64_t)height;
    uint8_t *work = NULL;
    uint8_t *tmp = NULL;
    uint64_t accumulated_edges = 0;
    uint32_t pass;

    /*
     * Regiones muy pequenas no tienen vecinos suficientes para un filtro 3x3.
     */
    if (region_pixels == NULL || width < 3u || height < 3u || passes == 0u) {
        return 0;
    }

    work = (uint8_t *)malloc((size_t)total_pixels);
    tmp = (uint8_t *)malloc((size_t)total_pixels);

    if (work == NULL || tmp == NULL) {
        free(work);
        free(tmp);

        /*
         * Si no hay memoria para la carga pesada, no se cae el programa.
         * Solo se omite esta parte y se mantiene el procesamiento base.
         */
        return 0;
    }

    memcpy(work, region_pixels, (size_t)total_pixels);

    for (pass = 0; pass < passes; pass++) {
        uint32_t x;
        uint32_t y;

        /*
         * Mantener bordes sin modificar para evitar lecturas fuera de rango.
         */
        memcpy(tmp, work, (size_t)total_pixels);

        /*
         * Suavizado 3x3.
         */
        for (y = 1; y + 1 < height; y++) {
            for (x = 1; x + 1 < width; x++) {
                uint32_t i = (y * width) + x;

                uint32_t sum =
                    work[((y - 1u) * width) + (x - 1u)] +
                    work[((y - 1u) * width) + x] +
                    work[((y - 1u) * width) + (x + 1u)] +
                    work[(y * width) + (x - 1u)] +
                    work[(y * width) + x] +
                    work[(y * width) + (x + 1u)] +
                    work[((y + 1u) * width) + (x - 1u)] +
                    work[((y + 1u) * width) + x] +
                    work[((y + 1u) * width) + (x + 1u)];

                tmp[i] = (uint8_t)(sum / 9u);
            }
        }

        /*
         * Sobel aproximado sobre tmp.
         */
        for (y = 1; y + 1 < height; y++) {
            for (x = 1; x + 1 < width; x++) {
                int32_t gx =
                    -(int32_t)tmp[((y - 1u) * width) + (x - 1u)] +
                     (int32_t)tmp[((y - 1u) * width) + (x + 1u)] -
                    2 * (int32_t)tmp[(y * width) + (x - 1u)] +
                    2 * (int32_t)tmp[(y * width) + (x + 1u)] -
                    (int32_t)tmp[((y + 1u) * width) + (x - 1u)] +
                    (int32_t)tmp[((y + 1u) * width) + (x + 1u)];

                int32_t gy =
                    -(int32_t)tmp[((y - 1u) * width) + (x - 1u)] -
                    2 * (int32_t)tmp[((y - 1u) * width) + x] -
                    (int32_t)tmp[((y - 1u) * width) + (x + 1u)] +
                    (int32_t)tmp[((y + 1u) * width) + (x - 1u)] +
                    2 * (int32_t)tmp[((y + 1u) * width) + x] +
                    (int32_t)tmp[((y + 1u) * width) + (x + 1u)];

                uint32_t magnitude = abs_i32(gx) + abs_i32(gy);

                if (magnitude > SOBEL_THRESHOLD) {
                    accumulated_edges++;
                }
            }
        }

        /*
         * La salida suavizada de esta pasada se usa como entrada de la siguiente.
         */
        {
            uint8_t *swap = work;
            work = tmp;
            tmp = swap;
        }
    }

    free(work);
    free(tmp);

    return accumulated_edges;
}

/*
 * Deteccion de borde simple usada por el procesamiento base.
 * Se mantiene porque es barata y ayuda con imagenes pequenas.
 */
static int is_edge_pixel_in_region(const uint8_t *region_pixels,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t x,
                                   uint32_t y,
                                   uint32_t global_x,
                                   uint32_t global_y,
                                   uint32_t image_width,
                                   uint32_t image_height) {
    int current = is_active_pixel(region_pixels[(y * width) + x]);

    if (!current) {
        return 0;
    }

    /*
     * No marcamos automaticamente los bordes de cada region como contorno,
     * porque la imagen fue partida en una malla 3x3. Si se hiciera eso,
     * aparecerian lineas falsas justo donde se dividio la imagen.
     * Solo se considera borde fisico si el pixel esta en el borde real de
     * la imagen completa, o si un vecino local inmediato es fondo.
     */
    if (global_x == 0u || global_y == 0u ||
        global_x + 1u >= image_width || global_y + 1u >= image_height) {
        return 1;
    }

    if (x > 0u && !is_active_pixel(region_pixels[(y * width) + (x - 1u)])) return 1;
    if (x + 1u < width && !is_active_pixel(region_pixels[(y * width) + (x + 1u)])) return 1;
    if (y > 0u && !is_active_pixel(region_pixels[((y - 1u) * width) + x])) return 1;
    if (y + 1u < height && !is_active_pixel(region_pixels[((y + 1u) * width) + x])) return 1;

    return 0;
}

void process_worker_regions(const WorkerTaskHeader *header,
                            const uint8_t *payload,
                            WorkerResult *result) {
    uint32_t region_index;
    uint32_t payload_offset = 0;
    uint32_t heavy_passes = geoboard_get_processing_passes();

    memset(result, 0, sizeof(*result));

    result->magic = WORKER_RESULT_MAGIC;
    result->worker_index = header->worker_index;

    result->bbox_min_x = -1;
    result->bbox_min_y = -1;
    result->bbox_max_x = -1;
    result->bbox_max_y = -1;

    for (region_index = 0; region_index < header->region_count; region_index++) {
        const RegionInfo *region = &header->regions[region_index];
        const uint8_t *region_pixels = payload + payload_offset;
        uint32_t x;
        uint32_t y;

        /*
         * Carga pesada por region.
         * Esta cuenta se suma a edge_pixels para que el trabajo no sea
         * descartado por el compilador y para tener una metrica visible.
         */
        result->edge_pixels += run_heavy_filter_pipeline(region_pixels,
                                                         region->width,
                                                         region->height,
                                                         heavy_passes);

        /*
         * Procesamiento base: conteo de pixeles activos, bbox y mascara 8x8.
         */
        for (y = 0; y < region->height; y++) {
            for (x = 0; x < region->width; x++) {
                uint8_t pixel = region_pixels[(y * region->width) + x];

                if (is_active_pixel(pixel)) {
                    uint32_t global_x = region->start_x + x;
                    uint32_t global_y = region->start_y + y;

                    uint32_t mask_x = (global_x * 8u) / header->image_width;
                    uint32_t mask_y = (global_y * 8u) / header->image_height;

                    if (mask_x > 7u) mask_x = 7u;
                    if (mask_y > 7u) mask_y = 7u;

                    int is_edge = is_edge_pixel_in_region(region_pixels,
                                                          region->width,
                                                          region->height,
                                                          x,
                                                          y,
                                                          global_x,
                                                          global_y,
                                                          header->image_width,
                                                          header->image_height);

                    /*
                     * La mascara 8x8 final representa SOLO el contorno.
                     * Los pixeles activos internos se siguen contando para
                     * metricas y bounding box, pero no se encienden en la
                     * mascara que ve el usuario.
                     */
                    if (is_edge) {
                        result->mask[mask_y] |= (uint8_t)(1u << (7u - mask_x));
                        result->edge_pixels++;
                    }

                    result->active_pixels++;
                    result->has_content = 1u;

                    if (result->bbox_min_x < 0 ||
                        (int32_t)global_x < result->bbox_min_x) {
                        result->bbox_min_x = (int32_t)global_x;
                    }

                    if (result->bbox_min_y < 0 ||
                        (int32_t)global_y < result->bbox_min_y) {
                        result->bbox_min_y = (int32_t)global_y;
                    }

                    if (result->bbox_max_x < 0 ||
                        (int32_t)global_x > result->bbox_max_x) {
                        result->bbox_max_x = (int32_t)global_x;
                    }

                    if (result->bbox_max_y < 0 ||
                        (int32_t)global_y > result->bbox_max_y) {
                        result->bbox_max_y = (int32_t)global_y;
                    }
                }
            }
        }

        payload_offset += region->width * region->height;
    }
}
