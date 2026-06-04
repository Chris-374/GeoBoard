#!/usr/bin/env bash
# process_images_folder.sh
#
# Procesa una carpeta completa de imagenes PGM usando el flujo actual:
#
#   cliente MPI -> servidor/orquestador -> 3 workers -> mascara 8x8
#
# No usa sockets. No comparte archivos entre nodos para el procesamiento.
# Simplemente ejecuta el job OpenMPI una vez por cada imagen de la carpeta.
#
# Uso:
#   ./process_images_folder.sh [carpeta_imagenes] [heavy_passes]
#
# Ejemplos:
#   ./process_images_folder.sh ../images 20
#   ./process_images_folder.sh ../images 100
#
# Salidas:
#   server_output/batch_masks.txt
#   server_output/batch_logs/<imagen>.log
#   server_output/mask8x8_<imagen>.txt

set -u

IMAGE_DIR="${1:-../images}"
HEAVY_PASSES="${2:-20}"

CLIENT="./geoboard_client"
SERVER="./geoboard_server_cluster"
OUTPUT_DIR="server_output"
LOG_DIR="${OUTPUT_DIR}/batch_logs"
SUMMARY_FILE="${OUTPUT_DIR}/batch_masks.txt"

if [ ! -x "$CLIENT" ]; then
    echo "[ERROR] No existe o no es ejecutable: $CLIENT"
    echo "Compila el cliente primero, por ejemplo:"
    echo "  mpicc -Wall -Wextra -O2 client/geoboard_client_mpi_chacha20.c -o geoboard_client"
    exit 1
fi

if [ ! -x "$SERVER" ]; then
    echo "[ERROR] No existe o no es ejecutable: $SERVER"
    echo "Compila el servidor primero:"
    echo "  make clean"
    echo "  make"
    exit 1
fi

if [ ! -d "$IMAGE_DIR" ]; then
    echo "[ERROR] No existe la carpeta de imagenes: $IMAGE_DIR"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
mkdir -p "$LOG_DIR"

export GEOBOARD_HEAVY_PASSES="$HEAVY_PASSES"

{
    echo "GeoBoard - resumen batch de mascaras"
    echo "Fecha: $(date)"
    echo "Carpeta de imagenes: $IMAGE_DIR"
    echo "GEOBOARD_HEAVY_PASSES=$GEOBOARD_HEAVY_PASSES"
    echo
} > "$SUMMARY_FILE"

mapfile -t IMAGES < <(find "$IMAGE_DIR" -maxdepth 1 -type f \( -iname "*.pgm" -o -iname "*.ppm" -o -iname "*.raw" -o -iname "*.bmp" \) | sort)

if [ "${#IMAGES[@]}" -eq 0 ]; then
    echo "[ERROR] No se encontraron imagenes en: $IMAGE_DIR"
    echo "Formatos buscados: .pgm .ppm .raw .bmp"
    exit 1
fi

echo "[BATCH] Imagenes encontradas: ${#IMAGES[@]}"
echo "[BATCH] Heavy passes: $GEOBOARD_HEAVY_PASSES"
echo "[BATCH] Resumen: $SUMMARY_FILE"
echo

TOTAL_START=$(python3 - <<'PY'
import time
print(time.time())
PY
)

INDEX=1
for IMAGE in "${IMAGES[@]}"; do
    BASENAME="$(basename "$IMAGE")"
    NAME_NO_EXT="${BASENAME%.*}"
    LOG_FILE="${LOG_DIR}/${NAME_NO_EXT}.log"
    MASK_FILE="${OUTPUT_DIR}/mask8x8_${BASENAME}.txt"

    echo "============================================================"
    echo "[BATCH] ($INDEX/${#IMAGES[@]}) Procesando: $IMAGE"
    echo "[BATCH] Log: $LOG_FILE"

    START=$(python3 - <<'PY'
import time
print(time.time())
PY
)

    mpirun --oversubscribe \
        -np 1 "$CLIENT" "$IMAGE" \
        : -np 4 "$SERVER" 2>&1 | tee "$LOG_FILE"

    STATUS=${PIPESTATUS[0]}

    END=$(python3 - <<'PY'
import time
print(time.time())
PY
)

    ELAPSED=$(python3 - <<PY
start = float("$START")
end = float("$END")
print(f"{end - start:.3f}")
PY
)

    {
        echo "============================================================"
        echo "Imagen: $BASENAME"
        echo "Ruta: $IMAGE"
        echo "Tiempo: ${ELAPSED} s"
        echo "Estado mpirun: $STATUS"
        echo "Log: $LOG_FILE"
    } >> "$SUMMARY_FILE"

    if [ -f "$MASK_FILE" ]; then
        {
            echo "Mascara 8x8:"
            cat "$MASK_FILE"
            echo
        } >> "$SUMMARY_FILE"

        echo "[BATCH] Mascara encontrada: $MASK_FILE"
    else
        {
            echo "Mascara 8x8: NO GENERADA"
            echo
        } >> "$SUMMARY_FILE"

        echo "[BATCH] Advertencia: no se encontro la mascara esperada: $MASK_FILE"
    fi

    if [ "$STATUS" -ne 0 ]; then
        echo "[BATCH] Advertencia: mpirun termino con estado $STATUS para $BASENAME"
    fi

    INDEX=$((INDEX + 1))
    echo "[BATCH] Tiempo imagen: ${ELAPSED} s"
    echo
done

TOTAL_END=$(python3 - <<'PY'
import time
print(time.time())
PY
)

TOTAL_ELAPSED=$(python3 - <<PY
start = float("$TOTAL_START")
end = float("$TOTAL_END")
print(f"{end - start:.3f}")
PY
)

{
    echo "============================================================"
    echo "Tiempo total batch: ${TOTAL_ELAPSED} s"
    echo "Imagenes procesadas: ${#IMAGES[@]}"
} >> "$SUMMARY_FILE"

echo "============================================================"
echo "[BATCH] Terminado."
echo "[BATCH] Imagenes procesadas: ${#IMAGES[@]}"
echo "[BATCH] Tiempo total: ${TOTAL_ELAPSED} s"
echo "[BATCH] Lista de mascaras: $SUMMARY_FILE"
