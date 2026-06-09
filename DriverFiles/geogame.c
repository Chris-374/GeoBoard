/*
 * geogame.c  ---  Interaccion del GeoBoard (espacio de usuario).
 *
 *   gcc geogame.c -o geogame
 *   sudo ./geogame
 *
 * Flujo:
 *   1. Muestra la figura objetivo 2 s (modo "reproducir") y luego limpia.
 *   2. Un cursor parpadea en la matriz; los botones lo mueven.
 *   3. SELECT fija (o quita) el punto bajo el cursor.
 *   4. CHECK compara la figura del usuario con la objetivo:
 *        - igual  -> toda la matriz parpadea 5 veces y se reinicia.
 *        - distinta -> simplemente se reinicia.
 *
 * Toda la logica vive aqui (politica); el driver solo controla el
 * hardware (mecanismo). No requiere cambios en el modulo del kernel.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include "lib/geoboard_ioctl.h"

static int fd;

/* Envia el frame completo (8 bytes = 8 filas) a la matriz */
static void draw(const uint8_t *m)
{
	write(fd, m, 8);
}

/* Lee el boton presionado (o GEO_BTN_NONE) */
static int read_button(void)
{
	int b = GEO_BTN_NONE;
	ioctl(fd, GEO_READ_BUTTON, &b);
	return b;
}

/* Parpadeo de exito: toda la matriz 5 veces */
static void blink_success(void)
{
	uint8_t full[8], empty[8];
	int i;

	memset(full, 0xFF, sizeof(full));
	memset(empty, 0x00, sizeof(empty));
	for (i = 0; i < 5; i++) {
		draw(full);
		usleep(200000);
		draw(empty);
		usleep(200000);
	}
}

int main(void)
{
	/* Figura objetivo (cambiala por la mascara que quieras retar).
	 * Convencion: byte = fila, bit n = columna n. */
	const uint8_t target[8] = {
		0x00, 0x10, 0x28, 0x44, 0x7c, 0x00, 0x00, 0x00
	};

	uint8_t user[8];
	int cx, cy;       /* posicion del cursor (columna, fila) */
	int prev, tick;

	fd = open("/dev/geoboard", O_RDWR);
	if (fd < 0) {
		perror("open /dev/geoboard");
		return 1;
	}

	for (;;) {              /* cada vuelta = una partida */
		/* --- reinicio del estado --- */
		memset(user, 0, sizeof(user));
		cx = cy = 0;
		prev = GEO_BTN_NONE;
		tick = 0;

		/* mostrar el objetivo y luego limpiar */
		draw(target);
		sleep(2);

		/* --- bucle de juego --- */
		for (;;) {
			int b = read_button();
			uint8_t frame[8];

			/* actuar solo en una pulsacion nueva (deteccion de flanco) */
			if (b != prev && b != GEO_BTN_NONE) {
				switch (b) {
				case GEO_BTN_UP:    if (cy > 0) cy--; break;
				case GEO_BTN_DOWN:  if (cy < 7) cy++; break;
				case GEO_BTN_LEFT:  if (cx > 0) cx--; break;
				case GEO_BTN_RIGHT: if (cx < 7) cx++; break;
				case GEO_BTN_SELECT:
					user[cy] ^= (1 << cx);  /* fija/quita */
					break;
				case GEO_BTN_CHECK:
					if (memcmp(user, target, 8) == 0)
						blink_success();
					goto restart;           /* siempre reinicia */
				}
			}
			prev = b;

			/* render: puntos fijos + cursor parpadeante (XOR) */
			memcpy(frame, user, sizeof(frame));
			if ((tick / 8) % 2)             /* fase encendida del cursor */
				frame[cy] ^= (1 << cx);
			draw(frame);

			tick++;
			usleep(30000);                  /* ~30 ms por iteracion */
		}
restart:
		; /* vuelve a empezar una nueva partida */
	}

	close(fd);
	return 0;
}
