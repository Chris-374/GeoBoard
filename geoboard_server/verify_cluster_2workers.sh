#!/usr/bin/env bash
# verify_cluster_2workers.sh
#
# Verifica que el cluster con 2 workers esta listo antes de correr GeoBoard.
# Ejecutar desde la Raspberry Pi, dentro de geoboard_server/.
#
# Comprueba:
#   1. SSH sin contrasena a cada worker
#   2. mpicc/mpirun disponible en cada worker
#   3. Ejecutable geoboard_server_cluster presente en cada worker
#   4. Red MPI funcional (mpirun hostname)
#
# Uso:
#   ./verify_cluster_2workers.sh [hostfile]
#   ./verify_cluster_2workers.sh hosts_geoboard_2workers

set -uo pipefail

HOSTFILE="${1:-hosts_geoboard_2workers}"
PROJECT_DIR="${PROJECT_DIR:-$PWD}"
SERVER="${PROJECT_DIR}/geoboard_server_cluster"

PASS=0
FAIL=0

ok()   { echo "  [OK]   $*"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $*"; FAIL=$((FAIL + 1)); }
info() { echo "  [INFO] $*"; }

echo "============================================================"
echo "Verificacion del cluster GeoBoard (2 workers x86)"
echo "Hostfile: $HOSTFILE"
echo "============================================================"
echo

# ---------- 1. Hostfile existe ----------
echo "[ 1 ] Hostfile..."
if [ -f "$HOSTFILE" ]; then
    ok "Existe: $HOSTFILE"
    # Extraer workers (lineas que no son la Raspberry, sin comentarios)
    mapfile -t WORKERS < <(grep -vE '^\s*#|^\s*$' "$HOSTFILE" | awk '{print $1}' | grep -v "$(hostname)" | grep -v "localhost" | grep -v "raspberrypi" || true)
    info "Workers encontrados en hostfile: ${WORKERS[*]:-ninguno}"
else
    fail "No existe: $HOSTFILE"
    WORKERS=()
fi
echo

# ---------- 2. Ejecutables locales ----------
echo "[ 2 ] Ejecutables locales (Raspberry Pi)..."
if [ -x "$SERVER" ]; then
    ok "geoboard_server_cluster"
else
    fail "geoboard_server_cluster no existe o no es ejecutable. Ejecuta 'make'."
fi

if [ -x "${PROJECT_DIR}/geoboard_client" ]; then
    ok "geoboard_client"
else
    fail "geoboard_client no existe. Compila el cliente."
fi
echo

# ---------- 3. SSH y ejecutables en workers ----------
echo "[ 3 ] SSH y ejecutables en workers..."
for WORKER in "${WORKERS[@]}"; do
    echo "  -- Worker: $WORKER"

    # SSH sin contrasena
    if ssh -o BatchMode=yes -o ConnectTimeout=5 "$WORKER" "echo ok" >/dev/null 2>&1; then
        ok "SSH sin contrasena OK: $WORKER"
    else
        fail "SSH falla en: $WORKER (configura con setup_ssh_2workers.sh)"
        continue
    fi

    # mpirun disponible
    if ssh -o BatchMode=yes "$WORKER" "which mpirun" >/dev/null 2>&1; then
        MPI_VER=$(ssh "$WORKER" "mpirun --version 2>&1 | head -1")
        ok "mpirun disponible: $MPI_VER"
    else
        fail "mpirun NO encontrado en $WORKER. Instala: sudo apt install -y openmpi-bin libopenmpi-dev"
    fi

    # Ejecutable presente
    REMOTE_SERVER="\$HOME/geoboard_server/geoboard_server_cluster"
    if ssh -o BatchMode=yes "$WORKER" "test -x ${REMOTE_SERVER}" >/dev/null 2>&1; then
        ok "geoboard_server_cluster presente en $WORKER"
    else
        fail "geoboard_server_cluster NO encontrado en $WORKER. Ejecuta deploy_to_workers.sh"
    fi
done
echo

# ---------- 4. Prueba MPI basica ----------
echo "[ 4 ] Prueba MPI basica (mpirun hostname)..."
if [ -f "$HOSTFILE" ] && [ "${#WORKERS[@]}" -ge 2 ]; then
    MPI_OUT=$(mpirun --hostfile "$HOSTFILE" -np 4 hostname 2>&1 || true)
    if echo "$MPI_OUT" | grep -q "$(hostname)"; then
        ok "mpirun --hostfile ejecuto con exito"
        info "Salida: $(echo "$MPI_OUT" | tr '\n' ' ')"
    else
        fail "mpirun --hostfile fallo. Salida: $MPI_OUT"
    fi
else
    info "Saltando prueba MPI (hostfile no disponible o menos de 2 workers)"
fi
echo

# ---------- Resumen ----------
echo "============================================================"
echo "Resultado: $PASS checks OK, $FAIL checks FALLARON"
echo "============================================================"

if [ "$FAIL" -eq 0 ]; then
    echo
    echo "Cluster listo. Puedes ejecutar:"
    echo "  ./run_2workers.sh images/square_5000.pgm hosts_geoboard_2workers 20"
    exit 0
else
    echo
    echo "Corrige los errores antes de lanzar el cluster."
    exit 1
fi
