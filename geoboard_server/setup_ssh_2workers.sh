#!/usr/bin/env bash
# setup_ssh_2workers.sh
#
# Configura SSH sin contrasena desde la Raspberry Pi hacia los 2 workers.
# Ejecutar SOLO desde la Raspberry Pi.
#
# Uso:
#   ./setup_ssh_2workers.sh <IP_o_hostname_worker1> <IP_o_hostname_worker2> [usuario]
#
# Ejemplos:
#   ./setup_ssh_2workers.sh 192.168.1.21 192.168.1.22
#   ./setup_ssh_2workers.sh worker1 worker2 ubuntu
#   ./setup_ssh_2workers.sh worker1 worker2 pi

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "Uso: $0 <worker1> <worker2> [usuario]"
    echo "Ejemplo: $0 192.168.1.21 192.168.1.22"
    echo "Ejemplo: $0 worker1 worker2 ubuntu"
    exit 1
fi

WORKER1="$1"
WORKER2="$2"
REMOTE_USER="${3:-${USER}}"

echo "============================================================"
echo "Configurando SSH sin contrasena"
echo "  Worker 1: ${REMOTE_USER}@${WORKER1}"
echo "  Worker 2: ${REMOTE_USER}@${WORKER2}"
echo "============================================================"
echo

# Generar clave SSH si no existe
if [ ! -f "$HOME/.ssh/id_rsa" ]; then
    echo "[SSH] Generando clave SSH en esta Raspberry..."
    ssh-keygen -t rsa -b 2048 -N "" -f "$HOME/.ssh/id_rsa"
    echo "[SSH] Clave generada: ~/.ssh/id_rsa"
else
    echo "[SSH] Ya existe una clave SSH en ~/.ssh/id_rsa"
fi
echo

# Copiar clave publica a cada worker
for WORKER in "$WORKER1" "$WORKER2"; do
    echo "[SSH] Copiando clave publica a ${REMOTE_USER}@${WORKER}..."
    echo "      (Se te pedira la contrasena del worker UNA ULTIMA VEZ)"
    ssh-copy-id "${REMOTE_USER}@${WORKER}"
    echo "[SSH] OK: ${WORKER}"
    echo
done

# Verificar conectividad SSH
echo "[SSH] Verificando conexion a los workers..."
for WORKER in "$WORKER1" "$WORKER2"; do
    if ssh -o BatchMode=yes -o ConnectTimeout=5 "${REMOTE_USER}@${WORKER}" "hostname" 2>/dev/null; then
        echo "[SSH] OK: ${WORKER} responde sin contrasena."
    else
        echo "[SSH] ERROR: No se pudo conectar a ${WORKER} sin contrasena."
        echo "      Revisa la red, el usuario, o si sshd esta corriendo en el worker."
        exit 1
    fi
done

echo
echo "============================================================"
echo "[SSH] Listo. SSH sin contrasena configurado para ambos workers."
echo "============================================================"
echo
echo "Proximos pasos:"
echo "  1. Editar hosts_geoboard_2workers con las IPs/hostnames reales"
echo "  2. Copiar el ejecutable geoboard_server_cluster a cada worker"
echo "  3. Ejecutar: ./run_2workers.sh images/square_5000.pgm"
