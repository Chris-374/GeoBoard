#ifndef WORKER_PROCESSING_H
#define WORKER_PROCESSING_H

/*
 * worker_processing.h
 *
 * Logica que ejecutan los nodos de procesamiento.
 * Cada worker recibe 3 regiones, las binariza y genera:
 * - mascara parcial 8x8
 * - cantidad de pixeles activos
 * - bordes aproximados
 * - bounding box local
 */

#include <stdint.h>
#include "geoboard_protocol.h"

int is_active_pixel(uint8_t pixel);

void process_worker_regions(const WorkerTaskHeader *header,
                            const uint8_t *payload,
                            WorkerResult *result);

#endif
