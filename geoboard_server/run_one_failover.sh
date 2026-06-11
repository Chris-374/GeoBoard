#!/usr/bin/env bash
# run_one_failover.sh
#
# Ejecutar desde la Raspberry, dentro de geoboard_server.
#
# Uso:
#   ./run_one_failover.sh images/triangle_5000.pgm workers.conf 50
#
# Modo normal:
#   rank 0 -> cliente local en Raspberry
#   rank 1 -> servidor/orquestador local en Raspberry
#   rank 2,3,4 -> workers remotos
#
# Si un worker cae durante el mpirun, OpenMPI normalmente aborta el job.
# Este script detecta el fallo, revisa qué workers siguen vivos y relanza
# la misma imagen con los 2 workers sobrevivientes. El servidor actualizado
# reagrupa las 9 regiones entre los workers activos.

set -u

IMAGE_PATH="${1:-images/triangle_5000.pgm}"
WORKERS_FILE="${2:-workers.conf}"
HEAVY_PASSES="${3:-50}"

PROJECT_DIR="${PROJECT_DIR:-$PWD}"
CLIENT="${CLIENT:-$PROJECT_DIR/geoboard_client}"
SERVER="${SERVER:-$PROJECT_DIR/geoboard_server_cluster}"
SERVER_HOST="${SERVER_HOST:-localhost}"
SSH_CONNECT_TIMEOUT="${SSH_CONNECT_TIMEOUT:-3}"

if [ ! -f "$IMAGE_PATH" ]; then
    echo "[ERROR] No existe la imagen local en Raspberry: $IMAGE_PATH"
    exit 1
fi

if [ ! -f "$WORKERS_FILE" ]; then
    echo "[ERROR] No existe workers file: $WORKERS_FILE"
    echo "Copia workers.conf.example a workers.conf y edita los nombres/IP."
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

read_workers() {
    grep -vE '^\s*#' "$WORKERS_FILE" | sed '/^\s*$/d'
}

is_worker_alive() {
    local host="$1"
    ssh -o BatchMode=yes -o ConnectTimeout="$SSH_CONNECT_TIMEOUT" "$host" "test -x '$SERVER'" >/dev/null 2>&1
}

get_alive_workers() {
    local host
    while IFS= read -r host; do
        if is_worker_alive "$host"; then
            echo "$host"
        else
            echo "[WARN] Worker no disponible: $host" >&2
        fi
    done < <(read_workers)
}

run_with_workers() {
    local -a workers=("$@")
    local -a cmd
    local worker

    cmd=(mpirun --map-by slot
         -np 1 --host "$SERVER_HOST" "$CLIENT" "$IMAGE_PATH"
         : -np 1 --host "$SERVER_HOST" "$SERVER")

    for worker in "${workers[@]}"; do
        cmd+=( : -np 1 --host "$worker" "$SERVER" )
    done

    echo "[RUN] Imagen: $IMAGE_PATH"
    echo "[RUN] Raspberry/server host: $SERVER_HOST"
    echo "[RUN] Workers activos (${#workers[@]}): ${workers[*]}"
    echo "[RUN] GEOBOARD_HEAVY_PASSES=$GEOBOARD_HEAVY_PASSES"
    echo "[RUN] Comando MPI: ${cmd[*]}"
    echo

    "${cmd[@]}"
}

mapfile -t ALIVE < <(get_alive_workers)

if [ "${#ALIVE[@]}" -lt 3 ]; then
    echo "[ERROR] Para la prueba normal se ocupan 3 workers vivos. Vivos: ${#ALIVE[@]}"
    echo "[INFO] Revise SSH/red antes de iniciar la demo normal."
    exit 1
fi

# Primer intento: 3 workers.
run_with_workers "${ALIVE[@]:0:3}"
STATUS=$?

if [ "$STATUS" -eq 0 ]; then
    echo "[OK] Imagen procesada correctamente con 3 workers."
    exit 0
fi

echo
echo "[FAILOVER] El job MPI fallo. Esto es normal si el profesor apago un worker."
echo "[FAILOVER] Revalidando workers vivos y relanzando la misma imagen..."
sleep 2

mapfile -t ALIVE_AFTER < <(get_alive_workers)

if [ "${#ALIVE_AFTER[@]}" -lt 2 ]; then
    echo "[ERROR] No hay suficientes workers sobrevivientes. Vivos: ${#ALIVE_AFTER[@]}"
    exit 1
fi

echo "[FAILOVER] Relanzando con 2 workers sobrevivientes: ${ALIVE_AFTER[*]:0:2}"
run_with_workers "${ALIVE_AFTER[@]:0:2}"
STATUS=$?

if [ "$STATUS" -eq 0 ]; then
    echo "[OK] Failover completado: la imagen se reproceso con 2 workers."
else
    echo "[ERROR] El failover tambien fallo."
fi

exit "$STATUS"
