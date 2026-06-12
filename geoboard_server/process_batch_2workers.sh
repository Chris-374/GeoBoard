#!/usr/bin/env bash
# process_batch_2workers.sh
#
# Procesa TODA una carpeta de imagenes con 2 workers.
#
# Uso:
#   ./process_batch_2workers.sh [carpeta_imagenes] [hostfile] [heavy_passes]
#
# Ejemplo:
#   ./process_batch_2workers.sh images hosts_geoboard_2workers 30
#
# Salida:
#   server_output/batch_2workers_YYYY-MM-DD.txt  (resumen de todas las imagenes)
#   server_output/batch_logs/                    (log individual por imagen)

set -euo pipefail

IMAGE_DIR="${1:-images}"
HOSTFILE="${2:-hosts_geoboard_2workers}"
HEAVY_PASSES="${3:-20}"

PROJECT_DIR="${PROJECT_DIR:-$PWD}"
CLIENT="${PROJECT_DIR}/geoboard_client"
SERVER="${PROJECT_DIR}/geoboard_server_cluster"

OUTPUT_DIR="server_output"
LOG_DIR="${OUTPUT_DIR}/batch_logs"
TIMESTAMP="$(date +%Y-%m-%d_%H-%M-%S)"
SUMMARY_FILE="${OUTPUT_DIR}/batch_2workers_${TIMESTAMP}.txt"

# --- Validaciones ---
if [ ! -d "$IMAGE_DIR" ]; then
    echo "[ERROR] No existe la carpeta: $IMAGE_DIR"
    exit 1
fi

if [ ! -f "$HOSTFILE" ]; then
    echo "[ERROR] No existe el hostfile: $HOSTFILE"
    exit 1
fi

if [ ! -x "$CLIENT" ] || [ ! -x "$SERVER" ]; then
    echo "[ERROR] Faltan ejecutables. Ejecuta 'make' primero."
    exit 1
fi

mkdir -p "$OUTPUT_DIR" "$LOG_DIR"
export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"

mapfile -t IMAGES < <(find "$IMAGE_DIR" -maxdepth 1 -type f -iname "*.pgm" | sort)

if [ "${#IMAGES[@]}" -eq 0 ]; then
    echo "[ERROR] No se encontraron archivos .pgm en: $IMAGE_DIR"
    exit 1
fi

{
    echo "GeoBoard Batch - 2 Workers"
    echo "Fecha:            $(date)"
    echo "Carpeta imagenes: $IMAGE_DIR"
    echo "Hostfile:         $HOSTFILE"
    echo "Heavy passes:     $GEOBOARD_HEAVY_PASSES"
    echo "Total imagenes:   ${#IMAGES[@]}"
    echo "Ranks:"
    echo "  rank 0 -> cliente    (Raspberry Pi)"
    echo "  rank 1 -> servidor   (Raspberry Pi)"
    echo "  rank 2 -> worker 0   (laptop x86 #1)"
    echo "  rank 3 -> worker 1   (laptop x86 #2)"
    echo
} > "$SUMMARY_FILE"

echo "[BATCH] Iniciando procesamiento de ${#IMAGES[@]} imagenes..."
echo "[BATCH] Resumen: $SUMMARY_FILE"
echo

TOTAL_START=$(date +%s)
INDEX=1
ERRORES=0

for IMAGE in "${IMAGES[@]}"; do
    BASENAME="$(basename "$IMAGE")"
    NAME_NO_EXT="${BASENAME%.*}"
    LOG_FILE="${LOG_DIR}/${NAME_NO_EXT}.log"
    MASK_FILE="${OUTPUT_DIR}/mask8x8_${BASENAME}.txt"

    echo "============================================================"
    echo "[BATCH] ($INDEX/${#IMAGES[@]}) Procesando: $BASENAME"

    START=$(date +%s)

    set +e
    mpirun --hostfile "$HOSTFILE" \
      -np 1 "$CLIENT" "$IMAGE" \
      : -np 3 "$SERVER" 2>&1 | tee "$LOG_FILE"
    STATUS=${PIPESTATUS[0]}
    set -e

    END=$(date +%s)
    ELAPSED=$((END - START))

    {
        echo "============================================================"
        echo "Imagen:  $BASENAME"
        echo "Tiempo:  ${ELAPSED} s"
        echo "Estado:  $STATUS"
        echo "Log:     $LOG_FILE"
    } >> "$SUMMARY_FILE"

    if [ -f "$MASK_FILE" ]; then
        {
            echo "Mascara 8x8:"
            cat "$MASK_FILE"
            echo
        } >> "$SUMMARY_FILE"
    else
        {
            echo "Mascara 8x8: NO GENERADA"
            echo
        } >> "$SUMMARY_FILE"
        ERRORES=$((ERRORES + 1))
    fi

    INDEX=$((INDEX + 1))
done

TOTAL_END=$(date +%s)
TOTAL_ELAPSED=$((TOTAL_END - TOTAL_START))

{
    echo "============================================================"
    echo "Tiempo total: ${TOTAL_ELAPSED} s"
    echo "Imagenes OK:  $((${#IMAGES[@]} - ERRORES))"
    echo "Errores:      $ERRORES"
} >> "$SUMMARY_FILE"

echo "============================================================"
echo "[BATCH] Terminado. Tiempo total: ${TOTAL_ELAPSED} s"
echo "[BATCH] Resumen guardado en: $SUMMARY_FILE"
