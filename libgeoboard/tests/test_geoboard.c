#include "geoboard.h"

#include <stdint.h>
#include <stdio.h>

static int matrices_equal(const uint8_t a[GEOBOARD_SIZE], const uint8_t b[GEOBOARD_SIZE]) {
    for (int i = 0; i < GEOBOARD_SIZE; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void move_cursor(GeoBoardButton btn, int *x, int *y) {
    switch (btn) {
        case GEOBOARD_BTN_UP:
            if (*y > 0) (*y)--;
            break;
        case GEOBOARD_BTN_DOWN:
            if (*y < GEOBOARD_SIZE - 1) (*y)++;
            break;
        case GEOBOARD_BTN_LEFT:
            if (*x > 0) (*x)--;
            break;
        case GEOBOARD_BTN_RIGHT:
            if (*x < GEOBOARD_SIZE - 1) (*x)++;
            break;
        default:
            break;
    }
}

static uint8_t bit_for_x(int x) {
    return (uint8_t)(1u << (GEOBOARD_SIZE - 1 - x));
}

static void toggle_pixel(uint8_t matrix[GEOBOARD_SIZE], int x, int y) {
    matrix[y] ^= bit_for_x(x);
}

int main(void) {
    const uint8_t target[GEOBOARD_SIZE] = {
        0x00, /* 00000000 */
        0x10, /* 00010000 */
        0x28, /* 00101000 */
        0x44, /* 01000100 */
        0x7C, /* 01111100 */
        0x00,
        0x00,
        0x00
    };

    uint8_t user_matrix[GEOBOARD_SIZE] = {0};
    int x = 0;
    int y = 0;

    if (geoboard_init() != GEOBOARD_OK) {
        return 1;
    }

    printf("Prueba de biblioteca GeoBoard.\n");
    printf("Primero se muestra la figura objetivo.\n");
    geoboard_draw_matrix(target);

    printf("Ahora se limpia la matriz. Reconstruya la figura con el teclado.\n");
    geoboard_clear();
    geoboard_set_cursor(x, y);

    while (1) {
        GeoBoardButton btn = geoboard_read_button();

        if (btn == GEOBOARD_BTN_QUIT) {
            break;
        }

        if (btn == GEOBOARD_BTN_SELECT) {
            toggle_pixel(user_matrix, x, y);
            geoboard_draw_matrix(user_matrix);
            geoboard_set_cursor(x, y);
            continue;
        }

        if (btn == GEOBOARD_BTN_CHECK) {
            if (matrices_equal(user_matrix, target)) {
                geoboard_success();
            } else {
                geoboard_error();
            }
            continue;
        }

        if (btn == GEOBOARD_BTN_RESET) {
            for (int i = 0; i < GEOBOARD_SIZE; i++) {
                user_matrix[i] = 0;
            }
            geoboard_clear();
            geoboard_set_cursor(x, y);
            continue;
        }

        move_cursor(btn, &x, &y);
        geoboard_set_cursor(x, y);
    }

    geoboard_reset_servo();
    geoboard_close();
    return 0;
}
