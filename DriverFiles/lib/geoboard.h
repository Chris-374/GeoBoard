#ifndef GEOBOARD_H
#define GEOBOARD_H

#include <stdint.h>

int geoboard_init(void);
int geoboard_clear(void);
int geoboard_draw_matrix(uint8_t matrix[8]);
int geoboard_set_pixel(int x, int y, int state);
int geoboard_set_brightness(int brightness);
int geoboard_close(void);

#endif
