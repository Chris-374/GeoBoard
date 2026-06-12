# Actualización: imágenes de cualquier tamaño

Esta versión mantiene el cliente con OpenMPI + ChaCha20, pero aclara y refuerza que el cliente puede recibir una imagen PGM de cualquier resolución.

Punto clave:

- El cliente no adapta la imagen.
- El cliente solo lee bytes, cifra con ChaCha20 y envía por MPI.
- El servidor adapta la imagen: divide proporcionalmente en 3x3 y luego reduce a máscara 8x8.
- No hace falta que la imagen sea 8x8.
- No hace falta que el ancho/alto sean múltiplos de 3.
- Se soportan incluso imágenes pequeñas donde algunas regiones de la malla 3x3 queden vacías.

Archivos relevantes:

```text
client/geoboard_client_mpi_chacha20.c
src/server_orchestrator.c
src/worker_node.c
src/worker_processing.c
```

Compilación del servidor:

```bash
make
```

Compilación del cliente:

```bash
mpicc -Wall -Wextra -O2 client/geoboard_client_mpi_chacha20.c -o geoboard_client
```

Ejecución:

```bash
mpirun -np 1 ./geoboard_client images/figura_cualquier_tamano.pgm : -np 4 ./geoboard_server_cluster
```
