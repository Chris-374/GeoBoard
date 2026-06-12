#!/usr/bin/env bash
# Ejecuta una imagen usando OpenMPI local, sin nodos remotos y sin MPMD (:).
# Todos los ranks corren el mismo binario en la misma Raspberry Pi:
#   rank 0 -> cliente
#   rank 1 -> servidor/orquestador
#   ranks 2,3,4 -> workers simulados como procesos MPI locales

set -euo pipefail

IMAGE_PATH="${1:-../images/cuadrado.pgm}"
HEAVY_PASSES="${2:-300}"
HW_SECONDS="${3:-10}"

if [ ! -f "$IMAGE_PATH" ]; then
    echo "[ERROR] No existe la imagen: $IMAGE_PATH"
    exit 1
fi

make

export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"
export GEOBOARD_HW_SECONDS="$HW_SECONDS"

echo "[RUN] Modo simulado local con OpenMPI, binario unico"
echo "[RUN] rank 0: cliente"
echo "[RUN] rank 1: servidor/orquestador"
echo "[RUN] ranks 2,3,4: workers MPI locales simulados"
echo "[RUN] Imagen: $IMAGE_PATH"
echo "[RUN] GEOBOARD_HEAVY_PASSES=$GEOBOARD_HEAVY_PASSES"
echo "[RUN] GEOBOARD_HW_SECONDS=$GEOBOARD_HW_SECONDS"
echo

mpirun --oversubscribe \
  -np 5 ./geoboard_server_cluster "$IMAGE_PATH"
