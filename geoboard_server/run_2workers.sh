#!/usr/bin/env bash
# run_2workers.sh
#
# Ejecuta UNA imagen con:
#   rank 0  -> cliente local (Raspberry Pi)
#   rank 1  -> servidor/orquestador local (Raspberry Pi)
#   rank 2  -> worker 0 (laptop x86 #1)
#   rank 3  -> worker 1 (laptop x86 #2)
#
# Total: 4 ranks = 1 cliente + 1 servidor + 2 workers
#
# Uso:
#   ./run_2workers.sh [imagen.pgm] [hostfile] [heavy_passes]
#
# Ejemplos:
#   ./run_2workers.sh images/square_5000.pgm hosts_geoboard_2workers 20
#   ./run_2workers.sh images/circle_5000.pgm hosts_geoboard_2workers 50
#
# Nota: ejecutar este script DESDE la Raspberry Pi, dentro de geoboard_server/

set -euo pipefail

IMAGE_PATH="${1:-images/square_5000.pgm}"
HOSTFILE="${2:-hosts_geoboard_2workers}"
HEAVY_PASSES="${3:-20}"

PROJECT_DIR="${PROJECT_DIR:-$PWD}"
CLIENT="${PROJECT_DIR}/geoboard_client"
SERVER="${PROJECT_DIR}/geoboard_server_cluster"

# --- Validaciones previas ---
if [ ! -f "$IMAGE_PATH" ]; then
    echo "[ERROR] No existe la imagen: $IMAGE_PATH"
    exit 1
fi

if [ ! -f "$HOSTFILE" ]; then
    echo "[ERROR] No existe el hostfile: $HOSTFILE"
    echo "  Edita hosts_geoboard_2workers con los hostnames/IPs reales."
    exit 1
fi

if [ ! -x "$CLIENT" ]; then
    echo "[ERROR] No existe o no es ejecutable: $CLIENT"
    echo "  Ejecuta 'make' dentro de geoboard_server/ primero."
    exit 1
fi

if [ ! -x "$SERVER" ]; then
    echo "[ERROR] No existe o no es ejecutable: $SERVER"
    echo "  Ejecuta 'make' dentro de geoboard_server/ primero."
    exit 1
fi

export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"

echo "============================================================"
echo "[GeoBoard] Modo: 2 workers (Raspberry Pi + 2 laptops x86)"
echo "[GeoBoard] Imagen:          $IMAGE_PATH"
echo "[GeoBoard] Hostfile:        $HOSTFILE"
echo "[GeoBoard] Heavy passes:    $GEOBOARD_HEAVY_PASSES"
echo "[GeoBoard] Ranks:"
echo "  rank 0  -> cliente      (Raspberry, este mismo host)"
echo "  rank 1  -> servidor     (Raspberry, este mismo host)"
echo "  rank 2  -> worker 0     (laptop x86 #1)"
echo "  rank 3  -> worker 1     (laptop x86 #2)"
echo "============================================================"
echo

# Con 2 workers el servidor activa automaticamente el modo failover
# (GEOBOARD_MIN_FAILOVER_WORKERS = 2), distribuyendo las 9 regiones
# entre los 2 workers: worker 0 recibe regiones 1-5, worker 1 recibe 6-9.
# (Ver server_orchestrator.c: region_row_start/end calculado proporcional)

mpirun --hostfile "$HOSTFILE" \
  -np 1 "$CLIENT" "$IMAGE_PATH" \
  : -np 3 "$SERVER"

echo
echo "[GeoBoard] Procesamiento terminado."
echo "[GeoBoard] Mascara 8x8 guardada en: server_output/"
