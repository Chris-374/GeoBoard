#!/usr/bin/env bash
# process_images_folder_cluster.sh
#
# Procesa TODA una carpeta de imágenes desde la Raspberry Pi.
#
# La Raspberry debe tener la carpeta images/ ya cargada con las imágenes.
# Por cada imagen se ejecuta:
#
#   rank 0 -> cliente local en Raspberry
#   rank 1 -> servidor/orquestador en Raspberry
#   rank 2 -> worker remoto 1
#   rank 3 -> worker remoto 2
#   rank 4 -> worker remoto 3
#
# Uso:
#   ./process_images_folder_cluster.sh images hosts_geoboard 50
#
# Salida principal:
#   server_output/batch_masks_cluster.txt

set -euo pipefail

IMAGE_DIR="${1:-images}"
HOSTFILE="${2:-hosts_geoboard}"
HEAVY_PASSES="${3:-20}"

PROJECT_DIR="${PROJECT_DIR:-$PWD}"
CLIENT="${PROJECT_DIR}/geoboard_client"
SERVER="${PROJECT_DIR}/geoboard_server_cluster"

OUTPUT_DIR="server_output"
LOG_DIR="${OUTPUT_DIR}/batch_cluster_logs"
SUMMARY_FILE="${OUTPUT_DIR}/batch_masks_cluster.txt"

if [ ! -d "$IMAGE_DIR" ]; then
    echo "[ERROR] No existe la carpeta local en Raspberry: $IMAGE_DIR"
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

mkdir -p "$OUTPUT_DIR" "$LOG_DIR"
export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"

mapfile -t IMAGES < <(find "$IMAGE_DIR" -maxdepth 1 -type f -iname "*.pgm" | sort)

if [ "${#IMAGES[@]}" -eq 0 ]; then
    echo "[ERROR] No se encontraron .pgm en: $IMAGE_DIR"
    exit 1
fi

{
    echo "GeoBoard batch - Raspberry como servidor"
    echo "Fecha: $(date)"
    echo "Imagenes locales en Raspberry: $IMAGE_DIR"
    echo "Hostfile: $HOSTFILE"
    echo "GEOBOARD_HEAVY_PASSES=$GEOBOARD_HEAVY_PASSES"
    echo "Cantidad de imagenes: ${#IMAGES[@]}"
    echo
} > "$SUMMARY_FILE"

echo "[BATCH] Raspberry como servidor/orquestador"
echo "[BATCH] Imagenes encontradas: ${#IMAGES[@]}"
echo "[BATCH] Resumen: $SUMMARY_FILE"
echo

TOTAL_START=$(date +%s)

INDEX=1
for IMAGE in "${IMAGES[@]}"; do
    BASENAME="$(basename "$IMAGE")"
    NAME_NO_EXT="${BASENAME%.*}"
    LOG_FILE="${LOG_DIR}/${NAME_NO_EXT}.log"
    MASK_FILE="${OUTPUT_DIR}/mask8x8_${BASENAME}.txt"

    echo "============================================================"
    echo "[BATCH] ($INDEX/${#IMAGES[@]}) Procesando: $IMAGE"

    START=$(date +%s)

    set +e
    mpirun --hostfile "$HOSTFILE" \
      -np 1 "$CLIENT" "$IMAGE" \
      : -np 4 "$SERVER" 2>&1 | tee "$LOG_FILE"
    STATUS=${PIPESTATUS[0]}
    set -e

    END=$(date +%s)
    ELAPSED=$((END - START))

    {
        echo "============================================================"
        echo "Imagen: $BASENAME"
        echo "Ruta local Raspberry: $IMAGE"
        echo "Tiempo aproximado: ${ELAPSED} s"
        echo "Estado mpirun: $STATUS"
        echo "Log: $LOG_FILE"
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
    fi

    if [ "$STATUS" -ne 0 ]; then
        echo "[BATCH] Advertencia: error procesando $BASENAME"
    fi

    INDEX=$((INDEX + 1))
done

TOTAL_END=$(date +%s)
TOTAL_ELAPSED=$((TOTAL_END - TOTAL_START))

{
    echo "============================================================"
    echo "Tiempo total batch aproximado: ${TOTAL_ELAPSED} s"
    echo "Imagenes procesadas: ${#IMAGES[@]}"
} >> "$SUMMARY_FILE"

echo "============================================================"
echo "[BATCH] Terminado."
echo "[BATCH] Resumen: $SUMMARY_FILE"
