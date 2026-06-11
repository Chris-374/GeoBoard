# Makefile del servidor modular de GeoBoard.
#
# Compila un unico ejecutable:
#   geoboard_server_cluster
#
# Este ejecutable debe correrse con OpenMPI junto al cliente.

CC = mpicc
CFLAGS = -Wall -Wextra -O2 -Iinclude

TARGET = geoboard_server_cluster

SRC = \
	src/main.c \
	src/geoboard_protocol.c \
	src/chacha20.c \
	src/file_utils.c \
	src/pgm_image.c \
	src/worker_processing.c \
	src/worker_node.c \
	src/server_orchestrator.c

OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run-example:
	@echo "Ejemplo:"
	@echo "mpirun -np 1 ./geoboard_client images/cuadrado.pgm : -np 4 ./$(TARGET)"
