#ifndef GEOBOARD_H
#define GEOBOARD_H

#include <stdint.h>
#include "geoboard_ioctl.h"

int geoboard_init(void);
int geoboard_clear(void);
int geoboard_draw_matrix(uint8_t matrix[8]);
int geoboard_set_pixel(int x, int y, int state);
int geoboard_set_brightness(int brightness);
int geoboard_read_button(void);
int geoboard_buzzer(int on);
int geoboard_servo(int up);
int geoboard_success(void);
int geoboard_error(void);
int geoboard_reset_servo(void);
int geoboard_close(void);

#endif
