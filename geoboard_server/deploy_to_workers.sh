#!/usr/bin/env bash
# deploy_to_workers.sh
#
# Copia el ejecutable geoboard_server_cluster compilado para x86
# a los dos workers laptops.
#
# IMPORTANTE:
# Los workers son x86 (laptops Intel), la Raspberry es ARM.
# El ejecutable compilado en la Raspberry NO corre en los workers.
# Hay dos opciones:
#
#   Opcion A (recomendada para la defensa):
#     Compilar el ejecutable en cada worker manualmente.
#     Ver instrucciones abajo.
#
#   Opcion B (automatica, si ya compilaste en cada worker):
#     Copiar los fuentes y compilar remotamente con este script.
#
# Uso:
#   ./deploy_to_workers.sh <worker1> <worker2> [usuario] [ruta_remota]
#
# Ejemplo:
#   ./deploy_to_workers.sh 192.168.1.21 192.168.1.22
#   ./deploy_to_workers.sh worker1 worker2 ubuntu /home/ubuntu/geoboard_server

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "Uso: $0 <worker1> <worker2> [usuario] [ruta_remota]"
    exit 1
fi

WORKER1="$1"
WORKER2="$2"
REMOTE_USER="${3:-${USER}}"
REMOTE_DIR="${4:-\$HOME/geoboard_server}"

PROJECT_DIR="${PROJECT_DIR:-$PWD}"

echo "============================================================"
echo "Deploy a workers x86"
echo "  Worker 1: ${REMOTE_USER}@${WORKER1}:${REMOTE_DIR}"
echo "  Worker 2: ${REMOTE_USER}@${WORKER2}:${REMOTE_DIR}"
echo "============================================================"
echo

for WORKER in "$WORKER1" "$WORKER2"; do
    echo "[DEPLOY] Creando directorio en ${WORKER}..."
    ssh "${REMOTE_USER}@${WORKER}" "mkdir -p ${REMOTE_DIR}/{src,include,images,server_output}"

    echo "[DEPLOY] Copiando fuentes a ${WORKER}..."
    # Copiar todos los fuentes y headers
    scp -r "${PROJECT_DIR}/src/"*.c    "${REMOTE_USER}@${WORKER}:${REMOTE_DIR}/src/"
    scp -r "${PROJECT_DIR}/include/"*.h "${REMOTE_USER}@${WORKER}:${REMOTE_DIR}/include/"
    scp    "${PROJECT_DIR}/Makefile"    "${REMOTE_USER}@${WORKER}:${REMOTE_DIR}/"

    echo "[DEPLOY] Compilando en ${WORKER} (necesita openmpi-dev instalado)..."
    ssh "${REMOTE_USER}@${WORKER}" "
        cd ${REMOTE_DIR} &&
        which mpicc || (echo 'ERROR: mpicc no encontrado. Instala openmpi: sudo apt install -y openmpi-bin libopenmpi-dev' && exit 1) &&
        make clean &&
        make &&
        echo 'Compilacion OK en ${WORKER}'
    "

    echo "[DEPLOY] OK: ${WORKER}"
    echo
done

echo "============================================================"
echo "[DEPLOY] Listo. Ejecutable compilado en ambos workers."
echo "============================================================"
echo
echo "Siguiente paso: ejecutar desde la Raspberry:"
echo "  ./run_2workers.sh images/square_5000.pgm hosts_geoboard_2workers 20"
