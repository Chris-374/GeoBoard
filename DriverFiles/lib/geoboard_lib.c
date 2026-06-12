#include "geoboard.h"
#include "geoboard_ioctl.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

static int geo_fd = -1;

int geoboard_init(void)
{
    geo_fd = open("/dev/geoboard", O_RDWR);
    return (geo_fd < 0) ? -1 : 0;
}

int geoboard_clear(void)
{
    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_CLEAR);
}

int geoboard_draw_matrix(uint8_t matrix[8])
{
    if (geo_fd < 0)
        return -1;

    return (write(geo_fd, matrix, 8) == 8) ? 0 : -1;
}

int geoboard_set_pixel(int x, int y, int state)
{
    if (geo_fd < 0)
        return -1;

    struct geo_pixel p = {
        .x = x,
        .y = y,
        .state = state
    };

    return ioctl(geo_fd, GEO_SET_PIXEL, &p);
}

int geoboard_set_brightness(int brightness)
{
    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_BRIGHTNESS, &brightness);
}

int geoboard_read_button(void)
{
    int button = GEO_BTN_NONE;

    if (geo_fd < 0)
        return GEO_BTN_NONE;

    if (ioctl(geo_fd, GEO_READ_BUTTON, &button) != 0)
        return GEO_BTN_NONE;

    return button;
}

int geoboard_buzzer(int on)
{
    int value = on ? 1 : 0;

    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_BUZZER, &value);
}

int geoboard_servo(int up)
{
    int value = up ? 1 : 0;

    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_SERVO, &value);
}

int geoboard_success(void)
{
    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_SUCCESS);
}

int geoboard_error(void)
{
    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_ERROR);
}

int geoboard_reset_servo(void)
{
    if (geo_fd < 0)
        return -1;

    return ioctl(geo_fd, GEO_RESET_SERVO);
}

int geoboard_close(void)
{
    int ret;

    if (geo_fd < 0)
        return -1;

    ret = close(geo_fd);
    geo_fd = -1;

    return ret;
}
