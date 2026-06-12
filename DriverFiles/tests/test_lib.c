#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "../lib/geoboard.h"

int main(void)
{
    if (geoboard_init() < 0) {
        perror("geoboard_init");
        return 1;
    }

    geoboard_clear();

    geoboard_set_pixel(0, 0, 1);
    sleep(1);

    uint8_t figura[8] = {
        0b00000000,
        0b00010000,
        0b00101000,
        0b01000100,
        0b01111100,
        0b00000000,
        0b00000000,
        0b00000000
    };

    geoboard_draw_matrix(figura);
    sleep(2);

    geoboard_clear();
    geoboard_close();

    return 0;
}
