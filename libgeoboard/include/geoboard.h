#ifndef GEOBOARD_H
#define GEOBOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEOBOARD_OK 0
#define GEOBOARD_ERROR -1
#define GEOBOARD_SIZE 8

/*
 * Convencion de coordenadas:
 * x: columna 0..7, de izquierda a derecha
 * y: fila 0..7, de arriba hacia abajo
 *
 * Convencion de mascara:
 * matrix[y] representa una fila.
 * El bit mas significativo corresponde a x = 0.
 * Ejemplo: 00010000 tiene encendido x = 3.
 */
typedef enum {
    GEOBOARD_BTN_NONE = 0,
    GEOBOARD_BTN_UP,
    GEOBOARD_BTN_DOWN,
    GEOBOARD_BTN_LEFT,
    GEOBOARD_BTN_RIGHT,
    GEOBOARD_BTN_SELECT,
    GEOBOARD_BTN_CHECK,
    GEOBOARD_BTN_RESET,
    GEOBOARD_BTN_QUIT
} GeoBoardButton;

int geoboard_init(void);
int geoboard_clear(void);
int geoboard_draw_matrix(const uint8_t matrix[GEOBOARD_SIZE]);
int geoboard_set_pixel(int x, int y, int state);
int geoboard_set_cursor(int x, int y);
GeoBoardButton geoboard_read_button(void);
int geoboard_success(void);
int geoboard_error(void);
int geoboard_reset_servo(void);
int geoboard_close(void);

#ifdef __cplusplus
}
#endif

#endif /* GEOBOARD_H */
