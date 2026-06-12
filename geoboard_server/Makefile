# Makefile del servidor modular de GeoBoard.
#
# Compila dos ejecutables:
#   geoboard_client          -> rank 0, lee/cifra/envia imagen por MPI
#   geoboard_server_cluster  -> modo unificado: rank 0 cliente, rank 1 servidor y ranks 2+ workers
#
# En modo simulado local se ejecutan todos los ranks en la misma Raspberry:
#   mpirun --oversubscribe -np 5 ./geoboard_server_cluster ../images/cuadrado.pgm

CC = mpicc
CFLAGS = -Wall -Wextra -O2 -Iinclude -I../DriverFiles/lib

SERVER_TARGET = geoboard_server_cluster
CLIENT_TARGET = geoboard_client
HW_LIB = ../DriverFiles/lib/libgeoboard.a

SERVER_SRC = \
	src/main.c \
	src/client_rank.c \
	src/geoboard_protocol.c \
	src/chacha20.c \
	src/file_utils.c \
	src/pgm_image.c \
	src/worker_processing.c \
	src/worker_node.c \
	src/server_orchestrator.c

SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_SRC = client/geoboard_client_mpi_chacha20.c

all: $(CLIENT_TARGET) $(SERVER_TARGET)

$(SERVER_TARGET): $(SERVER_OBJ) $(HW_LIB)
	$(CC) $(CFLAGS) $(SERVER_OBJ) $(HW_LIB) -o $(SERVER_TARGET)

$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_TARGET)

$(HW_LIB): ../DriverFiles/lib/geoboard_lib.c ../DriverFiles/lib/geoboard.h ../DriverFiles/lib/geoboard_ioctl.h
	gcc -O2 -Wall -Wextra -c ../DriverFiles/lib/geoboard_lib.c -o ../DriverFiles/lib/geoboard_lib.o
	ar rcs $(HW_LIB) ../DriverFiles/lib/geoboard_lib.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJ) $(SERVER_TARGET) $(CLIENT_TARGET) ../DriverFiles/lib/geoboard_lib.o ../DriverFiles/lib/libgeoboard.a

run-local-example: all
	@echo "Ejemplo local simulado en una sola Raspberry:"
	@echo "sudo -E env GEOBOARD_HEAVY_PASSES=300 mpirun --allow-run-as-root --oversubscribe -np 5 ./geoboard_server_cluster ../images/cuadrado.pgm"

.PHONY: all clean run-local-example
