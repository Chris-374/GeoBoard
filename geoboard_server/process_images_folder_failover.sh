#!/usr/bin/env bash
# process_images_folder_failover.sh
# Procesa una carpeta completa usando run_one_failover.sh por imagen.

set -u

IMAGE_DIR="${1:-images}"
WORKERS_FILE="${2:-workers.conf}"
HEAVY_PASSES="${3:-50}"

OUTPUT_DIR="server_output"
SUMMARY_FILE="$OUTPUT_DIR/batch_failover_summary.txt"
mkdir -p "$OUTPUT_DIR"

if [ ! -d "$IMAGE_DIR" ]; then
    echo "[ERROR] No existe carpeta: $IMAGE_DIR"
    exit 1
fi

mapfile -t IMAGES < <(find "$IMAGE_DIR" -maxdepth 1 -type f -iname "*.pgm" | sort)

if [ "${#IMAGES[@]}" -eq 0 ]; then
    echo "[ERROR] No se encontraron .pgm en $IMAGE_DIR"
    exit 1
fi

{
    echo "GeoBoard batch con failover"
    echo "Fecha: $(date)"
    echo "Imagenes: $IMAGE_DIR"
    echo "Workers: $WORKERS_FILE"
    echo "GEOBOARD_HEAVY_PASSES=$HEAVY_PASSES"
    echo
} > "$SUMMARY_FILE"

for image in "${IMAGES[@]}"; do
    base="$(basename "$image")"
    echo "============================================================"
    echo "[BATCH] Procesando $base"

    start=$(date +%s)
    ./run_one_failover.sh "$image" "$WORKERS_FILE" "$HEAVY_PASSES"
    status=$?
    end=$(date +%s)

    mask_file="$OUTPUT_DIR/mask8x8_${base}.txt"

    {
        echo "============================================================"
        echo "Imagen: $base"
        echo "Estado: $status"
        echo "Tiempo aproximado: $((end - start)) s"
        if [ -f "$mask_file" ]; then
            echo "Mascara 8x8:"
            cat "$mask_file"
        else
            echo "Mascara 8x8: NO GENERADA"
        fi
        echo
    } >> "$SUMMARY_FILE"

    if [ "$status" -ne 0 ]; then
        echo "[BATCH] Advertencia: fallo $base"
    fi
done

echo "[BATCH] Resumen: $SUMMARY_FILE"
