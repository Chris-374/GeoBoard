# Servidor modular MPI de GeoBoard Interactivo

Este paquete contiene una versión modularizada y comentada del servidor/orquestador con workers para el proyecto **GeoBoard Interactivo**.

No usa sockets. Toda la comunicación se hace con **OpenMPI** usando `MPI_Send` y `MPI_Recv`.

## Distribución de ranks

La ejecución esperada es:

```text
rank 0 -> cliente
rank 1 -> servidor/orquestador
rank 2 -> worker 1
rank 3 -> worker 2
rank 4 -> worker 3
```

El cliente se compila aparte. Este paquete genera el ejecutable del servidor + workers.

## Estructura

```text
geoboard_server_modular/
├── include/
│   ├── chacha20.h
│   ├── file_utils.h
│   ├── geoboard_protocol.h
│   ├── pgm_image.h
│   ├── server_orchestrator.h
│   ├── worker_node.h
│   └── worker_processing.h
├── src/
│   ├── chacha20.c
│   ├── file_utils.c
│   ├── geoboard_protocol.c
│   ├── main.c
│   ├── pgm_image.c
│   ├── server_orchestrator.c
│   ├── worker_node.c
│   └── worker_processing.c
├── server_output/
├── Makefile
└── README_server_modular.md
```

## Qué hace cada módulo

### `main.c`

Decide qué rol toma cada proceso según su rank global:

```text
rank 1 -> server_main()
rank 2, 3, 4 -> worker_main()
```

### `server_orchestrator.c`

Es el corazón del servidor. Hace lo siguiente:

```text
recibe imagen cifrada desde el cliente
guarda archivo cifrado
descifra con ChaCha20
guarda archivo descifrado
interpreta PGM P2/P5
divide la imagen en 9 regiones
envía 3 regiones a cada worker
recibe resultados parciales
consolida máscara final 8x8
guarda la máscara final
```

### `worker_node.c`

Recibe la tarea del servidor, descifra el payload y llama al procesamiento local.

### `worker_processing.c`

Procesa las regiones asignadas:

```text
binarización por umbral
conteo de pixeles activos
detección aproximada de bordes
bounding box local
máscara parcial 8x8
```

### `pgm_image.c`

Carga imágenes PGM:

```text
P2 -> PGM ASCII
P5 -> PGM binario
```

Para las primeras pruebas se recomienda usar PGM P2.

### `chacha20.c`

Implementa ChaCha20 directamente en C. Se usa para:

```text
descifrar imagen cliente -> servidor
cifrar regiones servidor -> worker
descifrar regiones dentro del worker
```

### `file_utils.c`

Crea la carpeta de salida, guarda archivos y escribe la máscara 8x8.

## Compilación

Se necesita OpenMPI instalado.

```bash
make
```

Equivale a compilar con `mpicc`.

## Ejecución

Ejemplo usando el cliente anterior:

```bash
mpirun -np 1 ./geoboard_client images/cuadrado.pgm : -np 4 ./geoboard_server_cluster
```

El cliente ocupa el `rank 0`.

El servidor modular ocupa 4 procesos:

```text
rank 1 -> servidor
rank 2 -> worker 1
rank 3 -> worker 2
rank 4 -> worker 3
```

## Salida generada

El servidor crea la carpeta:

```text
server_output/
```

Y guarda:

```text
encrypted_<nombre>.bin
decrypted_<nombre>
mask8x8_<nombre>.txt
```

## División de la imagen

La imagen se divide en una malla 3x3:

```text
[ Región 1 ][ Región 2 ][ Región 3 ]
[ Región 4 ][ Región 5 ][ Región 6 ]
[ Región 7 ][ Región 8 ][ Región 9 ]
```

Asignación:

```text
Worker rank 2 -> regiones 1, 2, 3
Worker rank 3 -> regiones 4, 5, 6
Worker rank 4 -> regiones 7, 8, 9
```

## Nota importante

La siguiente integración pendiente es conectar la máscara final 8x8 con `libgeoboard.a`, y luego con el driver del kernel que controlará GPIO.


## Soporte para imágenes de cualquier tamaño

El cliente puede recibir una imagen PGM de cualquier resolución positiva. El cliente no necesita saber el ancho ni el alto, porque solo lee el archivo como bytes, lo cifra y lo envía.

La adaptación ocurre en el servidor:

```text
imagen original de cualquier resolución
        ↓
lectura PGM P2/P5
        ↓
división proporcional en malla 3x3
        ↓
asignación de 3 regiones por worker
        ↓
reducción proporcional a máscara 8x8
```

No es obligatorio que el ancho o el alto sean múltiplos de 3. El servidor calcula los límites de cada región con división proporcional:

```text
x0 = ancho * columna / 3
x1 = ancho * (columna + 1) / 3
y0 = alto * fila / 3
y1 = alto * (fila + 1) / 3
```

Tampoco es obligatorio que la imagen ya sea 8x8. La máscara 8x8 se genera después, mapeando cada pixel activo a una celda de la matriz final.

Para imágenes extremadamente pequeñas, como 1x1 o 2x2, algunas regiones pueden quedar vacías. Esta versión lo permite: el worker que reciba una región vacía devuelve una máscara parcial vacía sin fallar.


## Modo de carga pesada para justificar la distribución

Esta versión agrega procesamiento real extra en cada worker:

```text
filtrado 3x3
detección de bordes tipo Sobel
múltiples pasadas configurables
```

No se usa `sleep()`. El tiempo aumenta porque cada worker procesa más operaciones por pixel.

La cantidad de pasadas se controla con:

```bash
export GEOBOARD_HEAVY_PASSES=8
```

Valores sugeridos:

```text
4   prueba normal
8   demo más pesada
12  demo bastante pesada
20  demo muy pesada
```

Ejemplo:

```bash
export GEOBOARD_HEAVY_PASSES=12
time mpirun --oversubscribe -np 1 ./geoboard_client images/heavy_10000.pgm : -np 4 ./geoboard_server_cluster
```

Cada worker imprime su tiempo local de procesamiento:

```text
[WORKER 2] Tiempo de procesamiento local: X.XXX s
```

Esto ayuda a defender que los workers sí están recibiendo carga de procesamiento y no solo datos pequeños.
