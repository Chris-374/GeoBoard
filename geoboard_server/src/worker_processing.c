/*
 * worker_processing.c
 *
 * Procesamiento local de cada worker.
 *
 * Cada worker recibe 3 regiones. El payload contiene los pixeles de esas
 * regiones concatenados en el mismo orden en que vienen descritas en el header.
 *
 * El procesamiento implementado es simple, pero defendible:
 * - binarizacion por umbral
 * - conteo de pixeles activos
 * - deteccion aproximada de bordes
 * - bounding box local
 * - reduccion a mascara parcial 8x8
 */

#include <string.h>
#include "worker_processing.h"

int is_active_pixel(uint8_t pixel) {
    return pixel < PIXEL_THRESHOLD;
}

/*
 * Un pixel activo se considera borde si:
 * - esta en el borde de su region, o
 * - tiene al menos un vecino directo no activo.
 */
static int is_edge_pixel_in_region(const uint8_t *region_pixels,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t x,
                                   uint32_t y) {
    int current = is_active_pixel(region_pixels[(y * width) + x]);

    if (!current) {
        return 0;
    }

    if (x == 0 || y == 0 || x + 1 >= width || y + 1 >= height) {
        return 1;
    }

    if (!is_active_pixel(region_pixels[(y * width) + (x - 1)])) return 1;
    if (!is_active_pixel(region_pixels[(y * width) + (x + 1)])) return 1;
    if (!is_active_pixel(region_pixels[((y - 1) * width) + x])) return 1;
    if (!is_active_pixel(region_pixels[((y + 1) * width) + x])) return 1;

    return 0;
}

void process_worker_regions(const WorkerTaskHeader *header,
                            const uint8_t *payload,
                            WorkerResult *result) {
    uint32_t region_index;
    uint32_t payload_offset = 0;

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

        for (y = 0; y < region->height; y++) {
            for (x = 0; x < region->width; x++) {
                uint8_t pixel = region_pixels[(y * region->width) + x];

                if (is_active_pixel(pixel)) {
                    uint32_t global_x = region->start_x + x;
                    uint32_t global_y = region->start_y + y;

                    /*
                     * Reduccion a matriz 8x8.
                     * Se mapea la coordenada global de la imagen a una celda
                     * de la mascara final.
                     */
                    uint32_t mask_x = (global_x * 8u) / header->image_width;
                    uint32_t mask_y = (global_y * 8u) / header->image_height;

                    if (mask_x > 7u) mask_x = 7u;
                    if (mask_y > 7u) mask_y = 7u;

                    result->mask[mask_y] |= (uint8_t)(1u << (7u - mask_x));
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

                    if (is_edge_pixel_in_region(region_pixels,
                                                region->width,
                                                region->height,
                                                x,
                                                y)) {
                        result->edge_pixels++;
                    }
                }
            }
        }

        payload_offset += region->width * region->height;
    }
}
