#include "geoboard.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GEOBOARD_DEVICE_PATH "/dev/geoboard"

typedef enum {
    BACKEND_SIM = 0,
    BACKEND_DRIVER = 1
} BackendMode;

static BackendMode backend = BACKEND_SIM;
static int driver_fd = -1;
static int initialized = 0;
static uint8_t current_matrix[GEOBOARD_SIZE];
static int cursor_x = 0;
static int cursor_y = 0;

static int is_valid_coord(int x, int y) {
    return x >= 0 && x < GEOBOARD_SIZE && y >= 0 && y < GEOBOARD_SIZE;
}

static uint8_t bit_for_x(int x) {
    return (uint8_t)(1u << (GEOBOARD_SIZE - 1 - x));
}

static int pixel_is_on(int x, int y) {
    return (current_matrix[y] & bit_for_x(x)) != 0;
}

static void sim_render(void) {
    printf("\n=== GeoBoard SIM 8x8 ===\n");
    printf("Controles: w/a/s/d mover | e select | c check | r reset | q salir\n\n");

    for (int y = 0; y < GEOBOARD_SIZE; y++) {
        for (int x = 0; x < GEOBOARD_SIZE; x++) {
            int on = pixel_is_on(x, y);

            if (x == cursor_x && y == cursor_y) {
                printf(on ? "[@]" : "[ ]");
            } else {
                printf(on ? " # " : " . ");
            }
        }
        printf("\n");
    }
    printf("\n");
    fflush(stdout);
}

static int driver_send_command(const char *fmt, ...) {
    if (driver_fd < 0) {
        fprintf(stderr, "GeoBoard error: el driver no esta abierto.\n");
        return GEOBOARD_ERROR;
    }

    char buffer[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len < 0 || len >= (int)sizeof(buffer)) {
        fprintf(stderr, "GeoBoard error: comando demasiado largo.\n");
        return GEOBOARD_ERROR;
    }

    ssize_t written = write(driver_fd, buffer, (size_t)len);
    if (written != len) {
        perror("GeoBoard write");
        return GEOBOARD_ERROR;
    }

    return GEOBOARD_OK;
}

int geoboard_init(void) {
    if (initialized) {
        return GEOBOARD_OK;
    }

    const char *mode = getenv("GEOBOARD_BACKEND");
    if (mode != NULL && strcmp(mode, "driver") == 0) {
        backend = BACKEND_DRIVER;
        driver_fd = open(GEOBOARD_DEVICE_PATH, O_RDWR);
        if (driver_fd < 0) {
            perror("No se pudo abrir /dev/geoboard");
            return GEOBOARD_ERROR;
        }
    } else {
        backend = BACKEND_SIM;
        printf("GeoBoard: usando backend simulado.\n");
    }

    memset(current_matrix, 0, sizeof(current_matrix));
    cursor_x = 0;
    cursor_y = 0;
    initialized = 1;

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("INIT\n");
    }

    sim_render();
    return GEOBOARD_OK;
}

int geoboard_clear(void) {
    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    memset(current_matrix, 0, sizeof(current_matrix));

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("CLEAR\n");
    }

    sim_render();
    return GEOBOARD_OK;
}

int geoboard_draw_matrix(const uint8_t matrix[GEOBOARD_SIZE]) {
    if (matrix == NULL) {
        fprintf(stderr, "GeoBoard error: matrix es NULL.\n");
        return GEOBOARD_ERROR;
    }

    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    memcpy(current_matrix, matrix, sizeof(current_matrix));

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("DRAW %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                   matrix[0], matrix[1], matrix[2], matrix[3],
                                   matrix[4], matrix[5], matrix[6], matrix[7]);
    }

    sim_render();
    return GEOBOARD_OK;
}

int geoboard_set_pixel(int x, int y, int state) {
    if (!is_valid_coord(x, y)) {
        fprintf(stderr, "GeoBoard error: coordenada fuera de rango x=%d y=%d.\n", x, y);
        return GEOBOARD_ERROR;
    }

    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    if (state) {
        current_matrix[y] |= bit_for_x(x);
    } else {
        current_matrix[y] &= (uint8_t)~bit_for_x(x);
    }

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("PIXEL %d %d %d\n", x, y, state ? 1 : 0);
    }

    sim_render();
    return GEOBOARD_OK;
}

int geoboard_set_cursor(int x, int y) {
    if (!is_valid_coord(x, y)) {
        fprintf(stderr, "GeoBoard error: cursor fuera de rango x=%d y=%d.\n", x, y);
        return GEOBOARD_ERROR;
    }

    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    cursor_x = x;
    cursor_y = y;

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("CURSOR %d %d\n", x, y);
    }

    sim_render();
    return GEOBOARD_OK;
}

static GeoBoardButton map_button_char(char ch) {
    switch (ch) {
        case 'w': return GEOBOARD_BTN_UP;
        case 's': return GEOBOARD_BTN_DOWN;
        case 'a': return GEOBOARD_BTN_LEFT;
        case 'd': return GEOBOARD_BTN_RIGHT;
        case 'e': return GEOBOARD_BTN_SELECT;
        case 'c': return GEOBOARD_BTN_CHECK;
        case 'r': return GEOBOARD_BTN_RESET;
        case 'q': return GEOBOARD_BTN_QUIT;
        default: return GEOBOARD_BTN_NONE;
    }
}

GeoBoardButton geoboard_read_button(void) {
    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_BTN_NONE;
    }

    if (backend == BACKEND_DRIVER) {
        char ch = '\0';
        ssize_t n = read(driver_fd, &ch, 1);
        if (n <= 0) {
            if (errno != 0) {
                perror("GeoBoard read");
            }
            return GEOBOARD_BTN_NONE;
        }
        return map_button_char(ch);
    }

    printf("Boton> ");
    fflush(stdout);

    int ch;
    do {
        ch = getchar();
    } while (ch == '\n' || ch == '\r');

    int discard;
    while ((discard = getchar()) != '\n' && discard != EOF) {
        /* descartar el resto de la linea */
    }

    return map_button_char((char)ch);
}

int geoboard_success(void) {
    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("SUCCESS\n");
    }

    printf("[GeoBoard SIM] SUCCESS: buzzer de exito + bandera arriba.\n");
    return GEOBOARD_OK;
}

int geoboard_error(void) {
    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("ERROR\n");
    }

    printf("[GeoBoard SIM] ERROR: buzzer de error + patron de fallo.\n");
    return GEOBOARD_OK;
}

int geoboard_reset_servo(void) {
    if (!initialized && geoboard_init() != GEOBOARD_OK) {
        return GEOBOARD_ERROR;
    }

    if (backend == BACKEND_DRIVER) {
        return driver_send_command("RESET_SERVO\n");
    }

    printf("[GeoBoard SIM] Servo reiniciado.\n");
    return GEOBOARD_OK;
}

int geoboard_close(void) {
    if (!initialized) {
        return GEOBOARD_OK;
    }

    if (backend == BACKEND_DRIVER) {
        driver_send_command("CLOSE\n");
        close(driver_fd);
        driver_fd = -1;
    }

    initialized = 0;
    backend = BACKEND_SIM;
    return GEOBOARD_OK;
}
