#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../lib/geoboard_ioctl.h"

int main(void)
{
    int fd = open("/dev/geoboard", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    const char *nombre[] = {
        "NONE", "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "CHECK"
    };
    int anterior = -1;

    printf("Presiona botones (Ctrl+C para salir)...\n");
    while (1) {
        int b = GEO_BTN_NONE;
        ioctl(fd, GEO_READ_BUTTON, &b);
        if (b != anterior) {               /* solo imprime cuando cambia */
            if (b >= 0 && b <= 6)
                printf("Boton: %s (%d)\n", nombre[b], b);
            anterior = b;
        }
        usleep(50000);                     /* 50 ms entre lecturas */
    }
    close(fd);
    return 0;
}
