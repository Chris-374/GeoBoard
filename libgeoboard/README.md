# libgeoboard

Biblioteca estatica inicial para el proyecto GeoBoard.

Esta primera version funciona sin hardware usando un backend simulado en consola.
Luego se puede usar con el driver real exportando:

```bash
export GEOBOARD_BACKEND=driver
```

En modo driver, la biblioteca intenta abrir `/dev/geoboard` y manda comandos de texto simples:

```text
INIT
CLEAR
DRAW 00 10 28 44 7C 00 00 00
PIXEL x y state
CURSOR x y
SUCCESS
ERROR
RESET_SERVO
CLOSE
```

## Compilar

```bash
make
```

Esto genera:

```text
build/libgeoboard.a
```

## Probar sin hardware

```bash
make run
```

Controles del simulador:

```text
w/a/s/d = mover cursor
e       = SELECT
c       = CHECK
r       = RESET
q       = salir
```
