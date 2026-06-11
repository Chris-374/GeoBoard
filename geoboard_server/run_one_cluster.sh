#!/usr/bin/env bash
# run_one_cluster.sh
#
# Ejecuta UNA imagen usando la Raspberry Pi como:
#   rank 0 -> cliente local
#   rank 1 -> servidor/orquestador
#
# y tres máquinas externas como workers:
#   rank 2, 3, 4 -> workers
#
# Uso:
#   ./run_one_cluster.sh images/triangle_5000.pgm hosts_geoboard 50
#
# Notas:
# - Ejecutar este script desde la Raspberry Pi.
# - La imagen debe existir en la Raspberry.
# - El proyecto debe existir en la misma ruta en todos los nodos, o se debe
#   ajustar PROJECT_DIR.
# - No se comparten archivos de imagen con los workers. La imagen viaja por MPI
#   cifrada en fragmentos.

set -euo pipefail

IMAGE_PATH="${1:-images/triangle_5000.pgm}"
HOSTFILE="${2:-hosts_geoboard}"
HEAVY_PASSES="${3:-20}"

PROJECT_DIR="${PROJECT_DIR:-$PWD}"
CLIENT="${PROJECT_DIR}/geoboard_client"
SERVER="${PROJECT_DIR}/geoboard_server_cluster"

if [ ! -f "$IMAGE_PATH" ]; then
    echo "[ERROR] No existe la imagen en la Raspberry: $IMAGE_PATH"
    exit 1
fi

if [ ! -f "$HOSTFILE" ]; then
    echo "[ERROR] No existe el hostfile: $HOSTFILE"
    echo "Copia hosts_geoboard.example a hosts_geoboard y edita los hostnames/IP."
    exit 1
fi

if [ ! -x "$CLIENT" ]; then
    echo "[ERROR] No existe o no es ejecutable: $CLIENT"
    exit 1
fi

if [ ! -x "$SERVER" ]; then
    echo "[ERROR] No existe o no es ejecutable: $SERVER"
    exit 1
fi

export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"

echo "[RUN] Raspberry como servidor/orquestador"
echo "[RUN] Imagen local: $IMAGE_PATH"
echo "[RUN] Hostfile: $HOSTFILE"
echo "[RUN] GEOBOARD_HEAVY_PASSES=$GEOBOARD_HEAVY_PASSES"
echo

mpirun --hostfile "$HOSTFILE" \
  -np 1 "$CLIENT" "$IMAGE_PATH" \
  : -np 4 "$SERVER"
