#ifndef CHACHA20_H
#define CHACHA20_H

/*
 * chacha20.h
 *
 * Implementacion local de ChaCha20.
 * ChaCha20 es un cifrado de flujo:
 * - genera un flujo de bytes pseudoaleatorios usando key + nonce + counter
 * - ese flujo se combina con los datos usando XOR
 *
 * La misma funcion sirve para cifrar y descifrar.
 */

#include <stdint.h>
#include "geoboard_protocol.h"

void chacha20_apply(uint8_t *data,
                    uint64_t size,
                    const uint8_t key[CHACHA20_KEY_SIZE],
                    const uint8_t nonce[CHACHA20_NONCE_SIZE],
                    uint32_t initial_counter);

void build_worker_nonce(uint32_t worker_index,
                        uint8_t nonce[CHACHA20_NONCE_SIZE]);

#endif
