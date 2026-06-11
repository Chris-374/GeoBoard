# GeoBoard - modo de carga pesada

Esta actualización agrega carga computacional real a los workers.

Antes, cada worker hacía un procesamiento muy liviano:

```text
binarización
conteo de pixeles
máscara 8x8
bordes simples
```

Ahora cada worker también ejecuta:

```text
filtro 3x3
detección de bordes tipo Sobel
varias pasadas sobre sus regiones
```

## Activar más carga

Use:

```bash
export GEOBOARD_HEAVY_PASSES=12
```

Luego ejecute:

```bash
time mpirun --oversubscribe -np 1 ./geoboard_client images/heavy_10000.pgm : -np 4 ./geoboard_server_cluster
```

## Valores recomendados

```text
GEOBOARD_HEAVY_PASSES=4   rápido
GEOBOARD_HEAVY_PASSES=8   carga media
GEOBOARD_HEAVY_PASSES=12  carga alta
GEOBOARD_HEAVY_PASSES=20  carga muy alta
```

## Volver a algo liviano

```bash
export GEOBOARD_HEAVY_PASSES=1
```

o borrar la variable:

```bash
unset GEOBOARD_HEAVY_PASSES
```

## Por qué esto es defendible

No se está usando `sleep`. El tiempo aumenta porque se hace más trabajo real de procesamiento de imagen por pixel. Esto está alineado con la propuesta del proyecto: binarización, filtrado, detección de bordes y extracción de métricas locales por nodo.
