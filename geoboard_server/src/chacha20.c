/*
 * chacha20.c
 *
 * Implementacion de ChaCha20 sin librerias externas.
 * Se usa para:
 * - descifrar la imagen que llega del cliente
 * - cifrar las regiones que el servidor envia a los workers
 * - descifrar las regiones dentro de cada worker
 *
 * ChaCha20 es simetrico:
 * aplicar chacha20_apply() una vez cifra;
 * aplicar chacha20_apply() otra vez con la misma key/nonce/counter descifra.
 */

#include <string.h>
#include "chacha20.h"

static uint32_t rotl32(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

static uint32_t load32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void store32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/* Operacion basica interna de ChaCha20. */
static void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

/*
 * Genera un bloque de 64 bytes de keystream.
 * Ese keystream se combina con los datos mediante XOR.
 */
static void chacha20_block(const uint8_t key[CHACHA20_KEY_SIZE],
                           uint32_t counter,
                           const uint8_t nonce[CHACHA20_NONCE_SIZE],
                           uint8_t output[CHACHA20_BLOCK_SIZE]) {
    static const uint32_t constants[4] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
    };

    uint32_t state[16];
    uint32_t working[16];
    int i;

    state[0] = constants[0];
    state[1] = constants[1];
    state[2] = constants[2];
    state[3] = constants[3];

    for (i = 0; i < 8; i++) {
        state[4 + i] = load32_le(key + (i * 4));
    }

    state[12] = counter;
    state[13] = load32_le(nonce + 0);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    for (i = 0; i < 16; i++) {
        working[i] = state[i];
    }

    /*
     * ChaCha20 usa 20 rondas:
     * 10 iteraciones, cada una con ronda de columnas + ronda diagonal.
     */
    for (i = 0; i < 10; i++) {
        quarter_round(&working[0], &working[4], &working[8],  &working[12]);
        quarter_round(&working[1], &working[5], &working[9],  &working[13]);
        quarter_round(&working[2], &working[6], &working[10], &working[14]);
        quarter_round(&working[3], &working[7], &working[11], &working[15]);

        quarter_round(&working[0], &working[5], &working[10], &working[15]);
        quarter_round(&working[1], &working[6], &working[11], &working[12]);
        quarter_round(&working[2], &working[7], &working[8],  &working[13]);
        quarter_round(&working[3], &working[4], &working[9],  &working[14]);
    }

    for (i = 0; i < 16; i++) {
        working[i] += state[i];
        store32_le(output + (i * 4), working[i]);
    }
}

void chacha20_apply(uint8_t *data,
                    uint64_t size,
                    const uint8_t key[CHACHA20_KEY_SIZE],
                    const uint8_t nonce[CHACHA20_NONCE_SIZE],
                    uint32_t initial_counter) {
    uint8_t keystream[CHACHA20_BLOCK_SIZE];
    uint64_t offset = 0;
    uint32_t counter = initial_counter;

    while (offset < size) {
        uint32_t i;
        uint64_t remaining = size - offset;
        uint32_t block_bytes = (remaining > CHACHA20_BLOCK_SIZE)
            ? CHACHA20_BLOCK_SIZE
            : (uint32_t)remaining;

        chacha20_block(key, counter, nonce, keystream);

        for (i = 0; i < block_bytes; i++) {
            data[offset + i] ^= keystream[i];
        }

        offset += block_bytes;
        counter++;
    }
}

/*
 * Nonce diferente para cada worker.
 * El servidor lo coloca en el header y el worker lo usa para descifrar.
 */
void build_worker_nonce(uint32_t worker_index,
                        uint8_t nonce[CHACHA20_NONCE_SIZE]) {
    memset(nonce, 0, CHACHA20_NONCE_SIZE);
    nonce[0] = 'G';
    nonce[1] = 'E';
    nonce[2] = 'O';
    nonce[3] = 'B';
    nonce[4] = 'W';
    nonce[5] = 'R';
    nonce[6] = 'K';
    nonce[7] = (uint8_t)worker_index;
    nonce[8] = 0x20;
    nonce[9] = 0x26;
    nonce[10] = 0x00;
    nonce[11] = (uint8_t)(worker_index + 1u);
}
