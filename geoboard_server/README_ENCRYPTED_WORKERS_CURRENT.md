# GeoBoard - servidor envía franjas cifradas a workers

Esta actualización aplica el cambio pedido sobre la versión actual del proyecto:

```text
Cliente -> envía imagen cifrada completa
Servidor -> recibe imagen cifrada
Servidor -> descifra solo el header PGM necesario
Servidor -> divide la imagen en franjas cifradas
Workers -> reciben franjas cifradas
Workers -> descifran localmente y procesan
```

## Por qué se cambió

Antes el servidor descifraba la imagen completa para extraer los píxeles y luego enviaba regiones a los workers. Ahora los workers son los que descifran las partes que reciben, lo cual calza mejor con el lineamiento de que la información transmitida por red vaya cifrada y con la idea de que los nodos procesen sus regiones.

## Archivos modificados

```text
geoboard_server/include/geoboard_protocol.h
geoboard_server/include/chacha20.h
geoboard_server/include/pgm_image.h
geoboard_server/src/chacha20.c
geoboard_server/src/pgm_image.c
geoboard_server/src/server_orchestrator.c
geoboard_server/src/worker_node.c
```

No se elimina ningún archivo.

## Cómo aplicar

Desde la raíz del repo:

```bash
unzip -o geoboard_encrypted_workers_current_update.zip
cd geoboard_server
make clean
make
```

## Importante

Este flujo nuevo requiere imágenes `PGM P5` binarias. El generador `scripts/generate_shape_pgm.py` ya genera PGM P5, así que funciona con este cambio.

Ejemplo:

```bash
python3 scripts/generate_shape_pgm.py triangle 5000 5000 ../images/triangle_5000.pgm
export GEOBOARD_HEAVY_PASSES=50
time mpirun --oversubscribe -np 1 ./geoboard_client ../images/triangle_5000.pgm : -np 4 ./geoboard_server_cluster
```

## Mensajes esperados

En el servidor deberías ver algo como:

```text
[SERVIDOR] El servidor NO descifra los pixeles completos; los workers descifran sus franjas.
[SERVIDOR] Enviadas regiones 1, 2, 3 al worker rank 2 como franja CIFRADA original.
```

En cada worker deberías ver algo como:

```text
[WORKER 2] Franja cifrada descifrada localmente. file_offset=...
```
