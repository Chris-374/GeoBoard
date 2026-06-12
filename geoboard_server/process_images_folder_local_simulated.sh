#!/usr/bin/env bash
set -euo pipefail

IMAGE_DIR="${1:-../images}"
HEAVY_PASSES="${2:-300}"
PAUSE_SECONDS="${3:-4}"
HW_SECONDS="${4:-6}"

if [ ! -d "$IMAGE_DIR" ]; then
    echo "[ERROR] No existe la carpeta: $IMAGE_DIR"
    exit 1
fi

make

export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"
export GEOBOARD_HW_SECONDS="$HW_SECONDS"

shopt -s nullglob
IMAGES=("$IMAGE_DIR"/*.pgm)

if [ ${#IMAGES[@]} -eq 0 ]; then
    echo "[ERROR] No hay imagenes .pgm en $IMAGE_DIR"
    exit 1
fi

for img in "${IMAGES[@]}"; do
    echo
    echo "============================================================"
    echo "[RUN] Procesando: $img"
    echo "============================================================"
    mpirun --oversubscribe -np 5 ./geoboard_server_cluster "$img"
    echo "[RUN] Esperando $PAUSE_SECONDS s antes de la siguiente imagen..."
    sleep "$PAUSE_SECONDS"
done
