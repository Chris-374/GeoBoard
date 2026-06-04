# Cliente MPI de GeoBoard Interactivo

Este módulo corresponde al cliente inicial del proyecto **GeoBoard Interactivo**. Su responsabilidad es cargar una imagen desde disco, cifrarla con **ChaCha20** y enviarla al servidor/orquestador usando únicamente **OpenMPI**.

No usa sockets, no usa HTTP y no se comunica por archivos compartidos. El envío se realiza con `MPI_Send` dentro de `MPI_COMM_WORLD`.

## Rol dentro del sistema

La distribución esperada del programa MPI es:

```text
rank 0 -> cliente
rank 1 -> servidor/orquestador
rank 2 -> worker/nodo de procesamiento 1
rank 3 -> worker/nodo de procesamiento 2
rank 4 -> worker/nodo de procesamiento 3
```

El cliente solo prepara y envía la imagen cifrada. No divide la imagen en regiones ni procesa la figura. Esa parte le corresponde al servidor y a los nodos de procesamiento.

Flujo general:

```text
Cliente
  lee imagen local
  cifra bytes con ChaCha20
  envía metadata al servidor por MPI
  envía imagen cifrada por MPI

Servidor
  recibe imagen cifrada
  guarda archivo cifrado
  descifra usando ChaCha20
  guarda archivo descifrado
  divide imagen en 9 regiones
  distribuye regiones entre 3 nodos
```

## Archivo fuente principal

```text
geoboard_client_mpi_chacha20.c
```

## Compilación

Para compilar se debe tener OpenMPI instalado.

```bash
mpicc -Wall -Wextra -O2 geoboard_client_mpi_chacha20.c -o geoboard_client
```

## Ejecución esperada

Cuando ya exista el servidor y los workers, la ejecución esperada sería algo como:

```bash
mpirun -np 1 ./geoboard_client images/cuadrado.pgm : -np 1 ./geoboard_server : -np 3 ./geoboard_worker
```

También se puede pasar un `counter` manual para ChaCha20:

```bash
mpirun -np 1 ./geoboard_client images/cuadrado.pgm 1 : -np 1 ./geoboard_server : -np 3 ./geoboard_worker
```

Si no se pasa el `counter`, se usa el valor por defecto:

```text
1
```

## Archivo de entrada que recibe el cliente

El cliente recibe como argumento la ruta de una imagen. Por ejemplo:

```bash
./geoboard_client images/cuadrado.pgm
```

El cliente **no interpreta la imagen**, solo la lee completa como bytes, la cifra y la manda al servidor. Sin embargo, para que el resto del proyecto pueda procesarla correctamente, el archivo debe seguir ciertas reglas.

## Formato recomendado: PGM

El formato más recomendado para el prototipo es **PGM**, porque es simple, se puede leer en C sin librerías externas y representa imágenes en escala de grises.

Se recomienda usar imágenes con:

- fondo claro o blanco
- figura oscura o negra
- una o varias figuras geométricas simples
- buena separación entre figura y fondo
- sin compresión
- tamaño suficientemente grande para justificar división en regiones

Ejemplos válidos de figuras:

```text
cuadrado
rectángulo
triángulo
círculo aproximado
línea
composición simple de figuras
```

## Tipos de PGM aceptables

Para facilitar el procesamiento posterior, se recomienda usar uno de estos dos tipos:

### PGM ASCII, tipo P2

Ejemplo mínimo:

```text
P2
8 8
255
255 255 255 255 255 255 255 255
255 255 255   0   0   0 255 255
255 255 255   0 255   0 255 255
255 255 255   0   0   0 255 255
255 255 255 255 255 255 255 255
255 255 255 255 255 255 255 255
255 255 255 255 255 255 255 255
255 255 255 255 255 255 255 255
```

En este ejemplo:

```text
255 = fondo blanco
0   = parte de la figura
```

### PGM binario, tipo P5

También se puede usar PGM binario porque ocupa menos espacio y es más parecido a una imagen real. Su estructura general es:

```text
P5
ancho alto
255
<bytes de pixeles>
```

Para las primeras pruebas, es más fácil usar `P2` porque se puede abrir y revisar con un editor de texto.

## Recomendación de tamaño

Para pruebas muy iniciales:

```text
8x8
16x16
24x24
```

Para pruebas más realistas del procesamiento distribuido:

```text
90x90
180x180
300x300
600x600
```

La propuesta del proyecto divide la imagen en una malla de 3x3, es decir, 9 regiones. Por eso conviene que el ancho y el alto sean múltiplos de 3 cuando sea posible.

Ejemplos recomendados:

```text
90x90
120x120
300x300
600x600
```

También es conveniente que la imagen pueda reducirse a una máscara final de 8x8.

## Ejemplo de imagen conceptual

Una imagen válida para el proyecto podría representar un triángulo negro sobre fondo blanco:

```text
fondo blanco: valores cercanos a 255
figura negra: valores cercanos a 0
```

Luego, después del procesamiento del servidor y los nodos, esa imagen se transformaría en una máscara 8x8 como:

```text
00000000
00010000
00101000
01000100
01111100
00000000
00000000
00000000
```

Esa máscara sería la que después se envía a la biblioteca y al driver para representar la figura en la matriz LED 8x8.

## Archivos que NO se recomiendan al inicio

Aunque el cliente puede leer cualquier archivo como bytes, no se recomienda iniciar con:

```text
PNG
JPG
JPEG
WEBP
GIF
```

La razón es que esos formatos normalmente están comprimidos y requieren librerías externas o parsers más complejos. Para este proyecto conviene mantener el procesamiento de imagen simple y defendible en C.

Tampoco se recomienda usar fotos reales al inicio. Es mejor usar imágenes artificiales con figuras geométricas claras.

## Reglas mínimas del archivo

El archivo que recibe el cliente debe cumplir:

1. Debe existir en la ruta indicada.
2. No debe estar vacío.
3. Debe poder abrirse en modo lectura binaria.
4. Debe representar una imagen que el servidor pueda procesar luego.
5. Preferiblemente debe ser `.pgm`.
6. Preferiblemente debe ser escala de grises.
7. Preferiblemente debe tener una figura oscura sobre fondo claro.
8. Preferiblemente debe tener ancho y alto múltiplos de 3.

## Qué envía el cliente al servidor

El cliente envía primero metadata y luego el archivo cifrado.

Metadata enviada:

```text
magic
version
file_size
filename_len
counter
chunk_size
nonce
filename
```

Después envía el contenido cifrado de la imagen en bloques de tamaño máximo:

```text
65536 bytes
```

Finalmente envía una marca de fin de archivo.

## Cifrado usado

El cliente usa **ChaCha20** implementado directamente en C.

ChaCha20 usa:

```text
key     -> 32 bytes
nonce   -> 12 bytes
counter -> 32 bits
```

En esta versión del prototipo:

- la `key` está fija en el código del cliente
- el servidor debe tener exactamente la misma `key`
- el `nonce` se envía como metadata por MPI
- el `counter` se envía como metadata por MPI

La misma función de ChaCha20 se usa para cifrar y descifrar. Es decir, el servidor descifra aplicando ChaCha20 otra vez sobre los bytes cifrados usando la misma `key`, el mismo `nonce` y el mismo `counter`.

## Importante sobre seguridad

Para fines del proyecto, esta implementación permite demostrar que la información transmitida por red viaja cifrada. En un sistema real, la key no debería estar escrita directamente en el código fuente, pero para este prototipo académico es una solución simple y defendible.

## Responsabilidades del cliente

El cliente sí hace:

```text
leer archivo
validar que no esté vacío
cifrar con ChaCha20
enviar metadata por MPI
enviar archivo cifrado por MPI
```

El cliente no hace:

```text
procesar imagen
detectar figuras
dividir en 9 regiones
ejecutar OpenMPI como worker
controlar GPIO
hablar con la biblioteca
hablar con el driver
```

## Posibles mensajes de error

Si se ejecuta sin imagen:

```text
Uso:
  ./geoboard_client <imagen.pgm|bmp|raw> [counter]
```

Si se ejecuta sin servidor dentro del mismo job MPI:

```text
[CLIENTE] Error: se necesita al menos rank 0 cliente y rank 1 servidor.
[CLIENTE] No use sockets; el servidor debe correr dentro del mismo job MPI.
```

Si el archivo no existe:

```text
[CLIENTE] No se pudo abrir el archivo 'images/cuadrado.pgm'
```

Si el archivo está vacío:

```text
[CLIENTE] El archivo esta vacio.
```

## Ejemplo recomendado para pruebas

Crear una carpeta:

```bash
mkdir -p images
```

Crear un archivo:

```bash
nano images/cuadrado.pgm
```

Contenido de ejemplo:

```text
P2
8 8
255
255 255 255 255 255 255 255 255
255 255   0   0   0   0 255 255
255 255   0 255 255   0 255 255
255 255   0 255 255   0 255 255
255 255   0 255 255   0 255 255
255 255   0   0   0   0 255 255
255 255 255 255 255 255 255 255
255 255 255 255 255 255 255 255
```

Compilar:

```bash
mpicc -Wall -Wextra -O2 geoboard_client_mpi_chacha20.c -o geoboard_client
```

Ejecutar con el servidor y los workers cuando estén listos:

```bash
mpirun -np 1 ./geoboard_client images/cuadrado.pgm : -np 1 ./geoboard_server : -np 3 ./geoboard_worker
```

## Nota para la defensa

Este cliente se justifica porque en el proyecto el archivo de entrada debe viajar cifrado y el procesamiento distribuido debe hacerse con OpenMPI. El cliente cumple la primera parte del flujo: tomar la imagen original, cifrarla y entregarla al servidor/orquestador sin usar sockets ni carpetas compartidas.
