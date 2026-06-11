#ifndef GEOBOARD_IOCTL_H
#define GEOBOARD_IOCTL_H

#ifdef __KERNEL__
# include <linux/ioctl.h>
# include <linux/types.h>
#else
# include <sys/ioctl.h>
# include <stdint.h>
#endif

#define GEO_IOC_MAGIC 'G'

struct geo_pixel { int x; int y; int state; };
struct geo_point  { int x; int y; };

#define GEO_CLEAR       _IO (GEO_IOC_MAGIC, 0)
#define GEO_SET_PIXEL   _IOW(GEO_IOC_MAGIC, 1, struct geo_pixel)
#define GEO_BRIGHTNESS  _IOW(GEO_IOC_MAGIC, 2, int)
#define GEO_SET_CURSOR  _IOW(GEO_IOC_MAGIC, 3, struct geo_point)
#define GEO_SUCCESS     _IO (GEO_IOC_MAGIC, 4)
#define GEO_ERROR       _IO (GEO_IOC_MAGIC, 5)
#define GEO_RESET_SERVO _IO (GEO_IOC_MAGIC, 6)
#define GEO_READ_BUTTON _IOR(GEO_IOC_MAGIC, 7, int)

#define GEO_BUZZER  _IOW(GEO_IOC_MAGIC, 9, int)   /* 1 = encender, 0 = apagar */

#define GEO_SERVO   _IOW(GEO_IOC_MAGIC, 10, int)   /* 1 = subir bandera, 0 = bajar */

#define GEO_IOC_MAXNR  10

//Button constants

#define GEO_BTN_NONE    0
#define GEO_BTN_UP      1
#define GEO_BTN_DOWN    2
#define GEO_BTN_LEFT    3
#define GEO_BTN_RIGHT   4
#define GEO_BTN_SELECT  5
#define GEO_BTN_CHECK   6

#endif
