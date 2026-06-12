# GeoBoard — Despliegue del Cluster con 2 Workers (Raspberry Pi + 2 Laptops x86)

## Arquitectura del cluster

```
+-------------------+      MPI       +-------------------+
|   Raspberry Pi    |<-------------->|  Laptop x86 #1    |
|  (ARM, Linux)     |      SSH       |  (worker 0)       |
|                   |                +-------------------+
|  rank 0: cliente  |      MPI       +-------------------+
|  rank 1: servidor |<-------------->|  Laptop x86 #2    |
+-------------------+      SSH       |  (worker 1)       |
                                     +-------------------+
```

La Raspberry Pi corre dos procesos MPI: el cliente (rank 0) que carga y cifra
la imagen, y el servidor/orquestador (rank 1) que distribuye el trabajo. Cada
laptop x86 corre un worker (ranks 2 y 3) que recibe una franja cifrada de la
imagen, la descifra localmente con ChaCha20 y procesa sus regiones.

### Distribucion de regiones con 2 workers

La imagen se divide en malla 3x3 (9 regiones). Con 2 workers el servidor
reagrupa las filas de la malla proporcionalemnte:

```
Worker 0 (rank 2) -> regiones 1, 2, 3, 4, 5  (filas 0 y 1 de la malla 3x3)
Worker 1 (rank 3) -> regiones 4, 5, 6, 7, 8, 9  (fila 1 mitad + fila 2)
```

> Nota: el reparto exacto lo calcula `server_orchestrator.c` con la formula
> `region_row_start = (worker_index * 3) / worker_count`. Con 2 workers:
> worker 0 recibe filas [0,1) de la malla y worker 1 recibe filas [1,3).
> Esto es el mecanismo de failover que ya estaba en el codigo.

---

## Requisitos previos

| Maquina         | SO                | Paquetes necesarios                              |
|-----------------|-------------------|--------------------------------------------------|
| Raspberry Pi 3B | Raspberry Pi OS   | `openmpi-bin libopenmpi-dev build-essential`     |
| Laptop x86 #1   | Ubuntu 22.04+     | `openmpi-bin libopenmpi-dev build-essential`     |
| Laptop x86 #2   | Ubuntu 22.04+     | `openmpi-bin libopenmpi-dev build-essential`     |

Todas las maquinas deben estar en la misma red local (LAN o WiFi).

---

## Paso 1 — Instalar OpenMPI en todas las maquinas

Ejecutar en **cada** maquina (Raspberry y ambas laptops):

```bash
sudo apt update
sudo apt install -y openmpi-bin libopenmpi-dev build-essential
# Verificar:
mpicc --version
mpirun --version
```

---

## Paso 2 — Configurar /etc/hosts (opcional pero recomendado)

Si no quieren usar IPs directamente, agregar en `/etc/hosts` de la Raspberry Pi:

```
192.168.X.YY    worker1
192.168.X.ZZ    worker2
```

Y en cada laptop agregar la IP de la Raspberry:

```
192.168.X.WW    raspberrypi
```

> Reemplaza las IPs con las reales de tu red. Para ver la IP: `ip addr show`

---

## Paso 3 — Configurar SSH sin contrasena (desde la Raspberry Pi)

OpenMPI necesita poder conectarse por SSH a los workers sin pedir contrasena.

```bash
# En la Raspberry Pi, dentro de geoboard_server/:
chmod +x setup_ssh_2workers.sh
./setup_ssh_2workers.sh worker1 worker2
# Si el usuario en las laptops es diferente:
./setup_ssh_2workers.sh 192.168.1.21 192.168.1.22 ubuntu
```

El script:
1. Genera `~/.ssh/id_rsa` si no existe
2. Copia la clave publica a cada worker con `ssh-copy-id`
3. Verifica que SSH funciona sin contrasena

**Prueba manual:**
```bash
ssh worker1 hostname   # debe responder sin pedir contrasena
ssh worker2 hostname
```

---

## Paso 4 — Compilar el ejecutable en cada laptop x86

**IMPORTANTE:** El ejecutable compilado en la Raspberry Pi (ARM) NO corre en las
laptops x86. Hay que compilar por separado en cada maquina.

### Opcion A — Automatica con deploy_to_workers.sh (desde la Raspberry)

```bash
chmod +x deploy_to_workers.sh
./deploy_to_workers.sh worker1 worker2
# Con usuario especifico:
./deploy_to_workers.sh 192.168.1.21 192.168.1.22 ubuntu
```

Esto copia los fuentes y ejecuta `make` en cada worker remotamente.

### Opcion B — Manual en cada laptop

En cada laptop (repetir en ambas):

```bash
# Clonar/copiar el proyecto a la laptop
# Opcion 1: via git (si tienen repositorio)
git clone <url_del_repo> geoboard_server
cd geoboard_server

# Opcion 2: copiar desde la Raspberry manualmente
scp -r pi@raspberrypi:~/geoboard_server/src     ~/geoboard_server/
scp -r pi@raspberrypi:~/geoboard_server/include ~/geoboard_server/
scp    pi@raspberrypi:~/geoboard_server/Makefile ~/geoboard_server/

# Compilar
cd ~/geoboard_server
make clean && make

# Verificar
ls -la geoboard_server_cluster
./geoboard_server_cluster   # debe mostrar: "Rank 0 pertenece al cliente..."
```

> El ejecutable debe quedar en la misma ruta que en la Raspberry para que los
> scripts funcionen sin modificaciones. Por defecto OpenMPI busca el binario
> en la ruta que le indicas en el hostfile o en el comando mpirun.

---

## Paso 5 — Configurar el hostfile

Editar `hosts_geoboard_2workers` con las IPs o hostnames reales:

```
# hosts_geoboard_2workers
raspberrypi slots=2    # <-- cambia por hostname o IP de tu Pi
worker1     slots=1    # <-- laptop x86 #1
worker2     slots=1    # <-- laptop x86 #2
```

Si usas IPs:
```
192.168.1.20 slots=2
192.168.1.21 slots=1
192.168.1.22 slots=1
```

---

## Paso 6 — Verificar el cluster

```bash
chmod +x verify_cluster_2workers.sh
./verify_cluster_2workers.sh hosts_geoboard_2workers
```

Salida esperada:
```
[OK]  Existe: hosts_geoboard_2workers
[OK]  geoboard_server_cluster
[OK]  geoboard_client
[OK]  SSH sin contrasena OK: worker1
[OK]  mpirun disponible: mpirun (Open MPI) X.Y.Z
[OK]  geoboard_server_cluster presente en worker1
[OK]  SSH sin contrasena OK: worker2
...
Resultado: 9 checks OK, 0 checks FALLARON
Cluster listo.
```

---

## Paso 7 — Compilar en la Raspberry Pi

```bash
cd geoboard_server
make clean && make
ls -la geoboard_server_cluster geoboard_client
```

---

## Paso 8 — Ejecutar

### Una sola imagen:

```bash
chmod +x run_2workers.sh
./run_2workers.sh images/square_5000.pgm hosts_geoboard_2workers 20
```

### Todas las imagenes de la carpeta:

```bash
chmod +x process_batch_2workers.sh
./process_batch_2workers.sh images hosts_geoboard_2workers 30
```

### Comando mpirun directo (para debug o defensa):

```bash
export GEOBOARD_HEAVY_PASSES=20

mpirun --hostfile hosts_geoboard_2workers \
  -np 1 ./geoboard_client images/square_5000.pgm \
  : -np 3 ./geoboard_server_cluster
```

> `-np 1` para el cliente + `-np 3` para el servidor = total 4 ranks.
> El servidor/orquestador usa rank 1 y los workers usan ranks 2 y 3.

---

## Salida esperada

```
[CLIENTE]  Cifrando contenido con ChaCha20...
[CLIENTE]  Enviando archivo cifrado al servidor MPI rank 1...

[SERVIDOR] MODO FAILOVER: se ejecuta con 2 workers sobrevivientes.
[SERVIDOR] La carga se reagrupara entre los workers activos.
[SERVIDOR] Archivo cifrado guardado en: server_output/encrypted_square_5000.pgm.bin
[SERVIDOR] Metadata PGM leida desde header descifrado: 5000x5000, max=255
[SERVIDOR] El servidor NO descifra los pixeles completos; los workers descifran sus franjas.
[SERVIDOR] Enviadas X regiones al worker rank 2 como franja CIFRADA original: 1,2,3,4,5,6
[SERVIDOR] Enviadas X regiones al worker rank 3 como franja CIFRADA original: 7,8,9

[WORKER 2] Franja cifrada descifrada localmente. file_offset=... bytes
[WORKER 3] Franja cifrada descifrada localmente. file_offset=... bytes
[WORKER 2] Tiempo de procesamiento local: X.XXX s
[WORKER 3] Tiempo de procesamiento local: X.XXX s

[SERVIDOR] Mascara final:
00000000
00111100
01000010
...
[SERVIDOR] Clasificacion basica: cuadrado o circulo aproximado
[SERVIDOR] Mascara final guardada en: server_output/mask8x8_square_5000.pgm.txt
```

> El mensaje "MODO FAILOVER" es normal con 2 workers. El codigo estaba disenado
> para 3 workers en modo normal, y reagrupa las 9 regiones entre los workers
> disponibles cuando hay menos. Con 2 workers funciona correctamente.

---

## Ajustar la carga de procesamiento (para la defensa)

La variable `GEOBOARD_HEAVY_PASSES` controla cuantas pasadas de filtrado Sobel
hace cada worker sobre sus regiones, para justificar el procesamiento distribuido:

```bash
export GEOBOARD_HEAVY_PASSES=10   # liviano, prueba rapida
export GEOBOARD_HEAVY_PASSES=30   # demo normal
export GEOBOARD_HEAVY_PASSES=50   # demo pesada, diferencia clara entre 1 y 2 workers
```

Para demostrar el beneficio del paralelismo en la defensa:

```bash
# Con 1 solo proceso (todo en la Raspberry, sin distribucion):
time mpirun --oversubscribe -np 1 ./geoboard_client images/square_5000.pgm \
  : -np 2 ./geoboard_server_cluster

# Con 2 workers distribuidos:
time mpirun --hostfile hosts_geoboard_2workers \
  -np 1 ./geoboard_client images/square_5000.pgm \
  : -np 3 ./geoboard_server_cluster
```

---

## Generacion de imagenes de prueba

```bash
# Instalar dependencias del generador
pip3 install pillow numpy

# Generar imagenes de distintas figuras y tamanos
python3 scripts/generate_shape_pgm.py square   5000 5000 images/square_5000.pgm
python3 scripts/generate_shape_pgm.py circle   5000 5000 images/circle_5000.pgm
python3 scripts/generate_shape_pgm.py triangle 5000 5000 images/triangle_5000.pgm

# Para demo mas pesada (mas pixeles = mas tiempo de procesamiento):
python3 scripts/generate_shape_pgm.py square 8000 8000 images/square_8000.pgm
```

---

## Soluciones a problemas comunes

| Problema | Causa probable | Solucion |
|---|---|---|
| `Permission denied (publickey)` | SSH sin contrasena no configurado | Ejecutar `setup_ssh_2workers.sh` |
| `mpicc: command not found` | OpenMPI no instalado | `sudo apt install -y libopenmpi-dev` |
| `Exec format error` | Ejecutable ARM corriendo en x86 | Compilar en cada laptop: `make` |
| `[SERVIDOR] Se necesitan al menos 4 ranks` | Pocos procesos en mpirun | Asegurar `-np 1 cliente : -np 3 servidor` |
| Worker no responde | Firewall o red | `ping worker1`, revisar `/etc/hosts` |
| Timeout SSH en mpirun | SSH lento en primera conexion | Agregar workers a `~/.ssh/known_hosts` con `ssh worker1 hostname` |

---

## Archivos entregados

| Archivo | Descripcion |
|---|---|
| `hosts_geoboard_2workers` | Hostfile para 2 workers |
| `workers_2.conf` | Lista de workers para scripts |
| `run_2workers.sh` | Ejecutar una imagen con 2 workers |
| `process_batch_2workers.sh` | Procesar toda la carpeta images/ |
| `setup_ssh_2workers.sh` | Configurar SSH sin contrasena |
| `deploy_to_workers.sh` | Compilar remotamente en los workers |
| `verify_cluster_2workers.sh` | Verificar que el cluster esta listo |
| `DESPLIEGUE_2WORKERS.md` | Esta guia |
